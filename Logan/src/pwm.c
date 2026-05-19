#include "pwm.h"

#include "main.h"

#define STEP_PWM_TIMER TIM3
#define STEP_PWM_CHANNEL LL_TIM_CHANNEL_CH1
#define STEP_PWM_GPIO_PORT GPIOB
#define STEP_PWM_GPIO_PIN LL_GPIO_PIN_4
#define STEP_PWM_GPIO_AF LL_GPIO_AF_2
#define STEP_PWM_DUTY_DIVISOR 2U
#define STEP_PWM_MIN_PERIOD_TICKS 2U
#define STEP_PWM_MAX_PERIOD_TICKS 65536U

static uint16_t current_frequency_hz = STEP_PWM_DEFAULT_FREQUENCY_HZ;
static bool running = false;

static void step_pwm_gpio_output_low(void)
{
    LL_GPIO_InitTypeDef gpio_init;

    LL_AHB1_GRP1_EnableClock(LL_AHB1_GRP1_PERIPH_GPIOB);
    LL_GPIO_ResetOutputPin(STEP_PWM_GPIO_PORT, STEP_PWM_GPIO_PIN);

    LL_GPIO_StructInit(&gpio_init);
    gpio_init.Pin = STEP_PWM_GPIO_PIN;
    gpio_init.Mode = LL_GPIO_MODE_OUTPUT;
    gpio_init.Speed = LL_GPIO_SPEED_FREQ_HIGH;
    gpio_init.OutputType = LL_GPIO_OUTPUT_PUSHPULL;
    gpio_init.Pull = LL_GPIO_PULL_NO;
    LL_GPIO_Init(STEP_PWM_GPIO_PORT, &gpio_init);
}

static void step_pwm_gpio_alternate(void)
{
    LL_GPIO_InitTypeDef gpio_init;

    LL_AHB1_GRP1_EnableClock(LL_AHB1_GRP1_PERIPH_GPIOB);

    LL_GPIO_StructInit(&gpio_init);
    gpio_init.Pin = STEP_PWM_GPIO_PIN;
    gpio_init.Mode = LL_GPIO_MODE_ALTERNATE;
    gpio_init.Speed = LL_GPIO_SPEED_FREQ_HIGH;
    gpio_init.OutputType = LL_GPIO_OUTPUT_PUSHPULL;
    gpio_init.Pull = LL_GPIO_PULL_NO;
    gpio_init.Alternate = STEP_PWM_GPIO_AF;
    LL_GPIO_Init(STEP_PWM_GPIO_PORT, &gpio_init);
}

static uint32_t step_pwm_timer_clock_hz(void)
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

static bool step_pwm_apply_frequency(uint16_t frequency_hz)
{
    uint32_t timer_clock_hz;
    uint32_t prescaler;
    uint32_t divisor;
    uint32_t period_ticks;
    uint32_t compare_ticks;

    if ((frequency_hz == 0U) || (frequency_hz > STEP_PWM_MAX_FREQUENCY_HZ))
    {
        return false;
    }

    timer_clock_hz = step_pwm_timer_clock_hz();
    if (timer_clock_hz == 0U)
    {
        return false;
    }

    prescaler = (timer_clock_hz + ((uint32_t)frequency_hz * STEP_PWM_MAX_PERIOD_TICKS) - 1U)
              / ((uint32_t)frequency_hz * STEP_PWM_MAX_PERIOD_TICKS);
    if (prescaler > 0U)
    {
        prescaler--;
    }

    if (prescaler > 0xFFFFU)
    {
        return false;
    }

    divisor = (prescaler + 1U) * (uint32_t)frequency_hz;
    period_ticks = (timer_clock_hz + divisor - 1U) / divisor;
    if (period_ticks < STEP_PWM_MIN_PERIOD_TICKS)
    {
        period_ticks = STEP_PWM_MIN_PERIOD_TICKS;
    }

    if (period_ticks > STEP_PWM_MAX_PERIOD_TICKS)
    {
        return false;
    }

    compare_ticks = period_ticks / STEP_PWM_DUTY_DIVISOR;
    if (compare_ticks == 0U)
    {
        compare_ticks = 1U;
    }

    LL_TIM_SetPrescaler(STEP_PWM_TIMER, prescaler);
    LL_TIM_SetAutoReload(STEP_PWM_TIMER, period_ticks - 1U);
    LL_TIM_OC_SetCompareCH1(STEP_PWM_TIMER, compare_ticks);
    LL_TIM_SetCounter(STEP_PWM_TIMER, 0U);
    LL_TIM_GenerateEvent_UPDATE(STEP_PWM_TIMER);

    current_frequency_hz = frequency_hz;
    return true;
}

void step_pwm_init(void)
{
    LL_TIM_InitTypeDef tim_init;
    LL_TIM_OC_InitTypeDef oc_init;

    running = false;
    step_pwm_gpio_output_low();

    LL_APB1_GRP1_EnableClock(LL_APB1_GRP1_PERIPH_TIM3);
    LL_TIM_DisableCounter(STEP_PWM_TIMER);

    LL_TIM_StructInit(&tim_init);
    tim_init.Prescaler = 0U;
    tim_init.CounterMode = LL_TIM_COUNTERMODE_UP;
    tim_init.Autoreload = 1U;
    tim_init.ClockDivision = LL_TIM_CLOCKDIVISION_DIV1;
    LL_TIM_Init(STEP_PWM_TIMER, &tim_init);
    LL_TIM_EnableARRPreload(STEP_PWM_TIMER);

    LL_TIM_OC_StructInit(&oc_init);
    oc_init.OCMode = LL_TIM_OCMODE_PWM1;
    oc_init.OCState = LL_TIM_OCSTATE_DISABLE;
    oc_init.OCPolarity = LL_TIM_OCPOLARITY_HIGH;
    oc_init.CompareValue = 0U;
    LL_TIM_OC_Init(STEP_PWM_TIMER, STEP_PWM_CHANNEL, &oc_init);
    LL_TIM_OC_EnablePreload(STEP_PWM_TIMER, STEP_PWM_CHANNEL);

    (void)step_pwm_apply_frequency(STEP_PWM_DEFAULT_FREQUENCY_HZ);
}

bool step_pwm_set_frequency_hz(uint16_t frequency_hz)
{
    return step_pwm_apply_frequency(frequency_hz);
}

uint16_t step_pwm_get_frequency_hz(void)
{
    return current_frequency_hz;
}

void step_pwm_start(void)
{
    step_pwm_gpio_alternate();
    LL_TIM_SetCounter(STEP_PWM_TIMER, 0U);
    LL_TIM_CC_EnableChannel(STEP_PWM_TIMER, STEP_PWM_CHANNEL);
    LL_TIM_EnableCounter(STEP_PWM_TIMER);
    running = true;
}

void step_pwm_stop(void)
{
    LL_TIM_DisableCounter(STEP_PWM_TIMER);
    LL_TIM_CC_DisableChannel(STEP_PWM_TIMER, STEP_PWM_CHANNEL);
    running = false;
    step_pwm_gpio_output_low();
}

bool step_pwm_is_running(void)
{
    return running;
}
