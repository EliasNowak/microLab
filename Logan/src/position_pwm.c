#include "position_pwm.h"

#include "main.h"

#define POSITION_PWM_TIMER TIM2
#define POSITION_PWM_TIMER_HZ 1000000U
#define POSITION_PWM_GPIO_PORT GPIOC
#define POSITION_PWM_GPIO_PIN LL_GPIO_PIN_0
#define POSITION_PWM_EXTI_LINE LL_EXTI_LINE_0

static volatile bool have_rise = false;
static volatile bool have_high = false;
static volatile uint32_t last_rise_tick = 0U;
static volatile uint32_t pending_high_us = 0U;

static volatile bool have_sample = false;
static volatile bool new_sample = false;
static volatile uint32_t latest_period_us = 0U;
static volatile uint32_t latest_high_us = 0U;

static uint32_t position_pwm_timer_clock_hz(void)
{
    uint32_t clock_hz;
    LL_RCC_ClocksTypeDef clocks;

    LL_RCC_GetSystemClocksFreq(&clocks);
    clock_hz = clocks.PCLK1_Frequency;

    if (LL_RCC_GetAPB1Prescaler() != LL_RCC_APB1_DIV_1)
    {
        clock_hz *= 2U;
    }

    return clock_hz;
}

static uint8_t position_pwm_duty_from_sample(uint32_t high_us, uint32_t period_us)
{
    uint32_t duty;

    if (period_us == 0U)
    {
        return 0U;
    }

    duty = ((high_us * POSITION_PWM_DUTY_MAX) + (period_us / 2U)) / period_us;
    if (duty > POSITION_PWM_DUTY_MAX)
    {
        duty = POSITION_PWM_DUTY_MAX;
    }

    return (uint8_t)duty;
}

static void position_pwm_fill_sample(position_pwm_sample_t *sample,
                                     uint32_t high_us,
                                     uint32_t period_us)
{
    uint8_t duty = position_pwm_duty_from_sample(high_us, period_us);

    sample->period_us = period_us;
    sample->high_us = high_us;
    sample->duty_5bit = duty;
    sample->area = 0U;
    sample->hardware_error = (duty == POSITION_PWM_ERROR_DUTY);
    sample->valid = (period_us >= POSITION_PWM_MIN_PERIOD_US)
                 && (period_us <= POSITION_PWM_MAX_PERIOD_US)
                 && (duty >= POSITION_PWM_MIN_AREA_DUTY)
                 && (duty <= POSITION_PWM_MAX_AREA_DUTY);

    if (sample->valid)
    {
        sample->area = duty - 1U;
    }
}

static void position_pwm_timer_init(void)
{
    uint32_t timer_clock_hz = position_pwm_timer_clock_hz();
    LL_TIM_InitTypeDef tim_init;

    LL_APB1_GRP1_EnableClock(LL_APB1_GRP1_PERIPH_TIM2);
    LL_TIM_DisableCounter(POSITION_PWM_TIMER);

    LL_TIM_StructInit(&tim_init);
    tim_init.Prescaler = (timer_clock_hz / POSITION_PWM_TIMER_HZ) - 1U;
    tim_init.CounterMode = LL_TIM_COUNTERMODE_UP;
    tim_init.Autoreload = 0xFFFFFFFFU;
    tim_init.ClockDivision = LL_TIM_CLOCKDIVISION_DIV1;
    LL_TIM_Init(POSITION_PWM_TIMER, &tim_init);
    LL_TIM_SetCounter(POSITION_PWM_TIMER, 0U);
    LL_TIM_EnableCounter(POSITION_PWM_TIMER);
}

static void position_pwm_gpio_exti_init(void)
{
    LL_GPIO_InitTypeDef gpio_init;

    LL_AHB1_GRP1_EnableClock(LL_AHB1_GRP1_PERIPH_GPIOC);
    LL_APB2_GRP1_EnableClock(LL_APB2_GRP1_PERIPH_SYSCFG);

    LL_GPIO_StructInit(&gpio_init);
    gpio_init.Pin = POSITION_PWM_GPIO_PIN;
    gpio_init.Mode = LL_GPIO_MODE_INPUT;
    gpio_init.Pull = LL_GPIO_PULL_NO;
    LL_GPIO_Init(POSITION_PWM_GPIO_PORT, &gpio_init);

    LL_SYSCFG_SetEXTISource(LL_SYSCFG_EXTI_PORTC, LL_SYSCFG_EXTI_LINE0);
    LL_EXTI_ClearFlag_0_31(POSITION_PWM_EXTI_LINE);
    LL_EXTI_EnableRisingTrig_0_31(POSITION_PWM_EXTI_LINE);
    LL_EXTI_EnableFallingTrig_0_31(POSITION_PWM_EXTI_LINE);
    LL_EXTI_EnableIT_0_31(POSITION_PWM_EXTI_LINE);

    NVIC_SetPriority(EXTI0_IRQn, 2U);
    NVIC_EnableIRQ(EXTI0_IRQn);
}

void position_pwm_init(void)
{
    have_rise = false;
    have_high = false;
    have_sample = false;
    new_sample = false;
    latest_period_us = 0U;
    latest_high_us = 0U;

    position_pwm_timer_init();
    position_pwm_gpio_exti_init();
}

bool position_pwm_has_sample(void)
{
    return have_sample;
}

bool position_pwm_get_latest(position_pwm_sample_t *sample)
{
    uint32_t high_us;
    uint32_t period_us;

    if ((sample == NULL) || !have_sample)
    {
        return false;
    }

    NVIC_DisableIRQ(EXTI0_IRQn);
    high_us = latest_high_us;
    period_us = latest_period_us;
    NVIC_EnableIRQ(EXTI0_IRQn);

    position_pwm_fill_sample(sample, high_us, period_us);
    return true;
}

bool position_pwm_take_new_sample(position_pwm_sample_t *sample)
{
    bool had_new;

    if (sample == NULL)
    {
        return false;
    }

    NVIC_DisableIRQ(EXTI0_IRQn);
    had_new = new_sample;
    NVIC_EnableIRQ(EXTI0_IRQn);

    if (!had_new || !position_pwm_get_latest(sample))
    {
        return false;
    }

    NVIC_DisableIRQ(EXTI0_IRQn);
    new_sample = false;
    NVIC_EnableIRQ(EXTI0_IRQn);
    return true;
}

void EXTI0_IRQHandler(void)
{
    uint32_t now;
    bool level_high;

    if (!LL_EXTI_IsActiveFlag_0_31(POSITION_PWM_EXTI_LINE))
    {
        return;
    }

    LL_EXTI_ClearFlag_0_31(POSITION_PWM_EXTI_LINE);
    now = LL_TIM_GetCounter(POSITION_PWM_TIMER);
    level_high = LL_GPIO_IsInputPinSet(POSITION_PWM_GPIO_PORT, POSITION_PWM_GPIO_PIN);

    if (level_high)
    {
        if (have_rise && have_high)
        {
            latest_period_us = now - last_rise_tick;
            latest_high_us = pending_high_us;
            have_sample = true;
            new_sample = true;
        }

        last_rise_tick = now;
        have_rise = true;
        have_high = false;
    }
    else if (have_rise)
    {
        pending_high_us = now - last_rise_tick;
        have_high = true;
    }
}
