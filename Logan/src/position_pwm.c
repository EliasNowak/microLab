#include "position_pwm.h"

#include "main.h"

#define POSITION_PWM_TIMER TIM2
#define POSITION_PWM_TIMER_HZ 1000000U
#define POSITION_PWM_ALL_EXTI_LINES (LL_EXTI_LINE_0 | LL_EXTI_LINE_1 | LL_EXTI_LINE_2 | LL_EXTI_LINE_10 | LL_EXTI_LINE_14)
#define POSITION_PWM_MIN_PERIOD_US 7000U
#define POSITION_PWM_MAX_PERIOD_US 9000U
#define POSITION_PWM_DUTY_MAX 31U
#define POSITION_PWM_ERROR_DUTY 1U
#define POSITION_PWM_MIN_AREA_DUTY 2U
#define POSITION_PWM_MAX_AREA_DUTY 18U
#define POSITION_PWM_CHANNEL_SIG1 0U
#define POSITION_PWM_CHANNEL_SIG2 1U
#define POSITION_PWM_CHANNEL_SIG3 2U
#define POSITION_PWM_CHANNEL_SIG4 3U
#define POSITION_PWM_CHANNEL_SIG5 4U

typedef struct
{
    GPIO_TypeDef *gpio_port;
    uint32_t gpio_pin;
    uint32_t exti_line;
    uint32_t syscfg_port;
    uint32_t syscfg_line;
    IRQn_Type irq;
} position_pwm_channel_config_t;

static const position_pwm_channel_config_t channel_config[POSITION_PWM_CHANNEL_COUNT] =
{
    { GPIOC, LL_GPIO_PIN_0,  LL_EXTI_LINE_0,  LL_SYSCFG_EXTI_PORTC, LL_SYSCFG_EXTI_LINE0,  EXTI0_IRQn },
    { GPIOC, LL_GPIO_PIN_2,  LL_EXTI_LINE_2,  LL_SYSCFG_EXTI_PORTC, LL_SYSCFG_EXTI_LINE2,  EXTI2_TSC_IRQn },
    { GPIOA, LL_GPIO_PIN_1,  LL_EXTI_LINE_1,  LL_SYSCFG_EXTI_PORTA, LL_SYSCFG_EXTI_LINE1,  EXTI1_IRQn },
    { GPIOB, LL_GPIO_PIN_10, LL_EXTI_LINE_10, LL_SYSCFG_EXTI_PORTB, LL_SYSCFG_EXTI_LINE10, EXTI15_10_IRQn },
    { GPIOB, LL_GPIO_PIN_14, LL_EXTI_LINE_14, LL_SYSCFG_EXTI_PORTB, LL_SYSCFG_EXTI_LINE14, EXTI15_10_IRQn }
};

static volatile bool have_rise[POSITION_PWM_CHANNEL_COUNT];
static volatile bool have_high[POSITION_PWM_CHANNEL_COUNT];
static volatile uint32_t last_rise_tick[POSITION_PWM_CHANNEL_COUNT];
static volatile uint32_t pending_high_us[POSITION_PWM_CHANNEL_COUNT];

static volatile bool have_sample[POSITION_PWM_CHANNEL_COUNT];
static volatile bool new_sample[POSITION_PWM_CHANNEL_COUNT];
static volatile uint32_t latest_period_us[POSITION_PWM_CHANNEL_COUNT];
static volatile uint32_t latest_high_us[POSITION_PWM_CHANNEL_COUNT];

static bool position_pwm_channel_valid(uint8_t channel)
{
    return channel < POSITION_PWM_CHANNEL_COUNT;
}

static void position_pwm_disable_irqs(void)
{
    NVIC_DisableIRQ(EXTI0_IRQn);
    NVIC_DisableIRQ(EXTI1_IRQn);
    NVIC_DisableIRQ(EXTI2_TSC_IRQn);
    NVIC_DisableIRQ(EXTI15_10_IRQn);
}

static void position_pwm_enable_irqs(void)
{
    NVIC_EnableIRQ(EXTI0_IRQn);
    NVIC_EnableIRQ(EXTI1_IRQn);
    NVIC_EnableIRQ(EXTI2_TSC_IRQn);
    NVIC_EnableIRQ(EXTI15_10_IRQn);
}

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
    uint8_t channel;

    LL_AHB1_GRP1_EnableClock(LL_AHB1_GRP1_PERIPH_GPIOA);
    LL_AHB1_GRP1_EnableClock(LL_AHB1_GRP1_PERIPH_GPIOB);
    LL_AHB1_GRP1_EnableClock(LL_AHB1_GRP1_PERIPH_GPIOC);
    LL_APB2_GRP1_EnableClock(LL_APB2_GRP1_PERIPH_SYSCFG);

    LL_GPIO_StructInit(&gpio_init);
    gpio_init.Mode = LL_GPIO_MODE_INPUT;
    gpio_init.Pull = LL_GPIO_PULL_NO;

    gpio_init.Pin = LL_GPIO_PIN_1;
    LL_GPIO_Init(GPIOA, &gpio_init);

    gpio_init.Pin = LL_GPIO_PIN_10 | LL_GPIO_PIN_14;
    LL_GPIO_Init(GPIOB, &gpio_init);

    gpio_init.Pin = LL_GPIO_PIN_0 | LL_GPIO_PIN_2;
    LL_GPIO_Init(GPIOC, &gpio_init);

    for (channel = 0U; channel < POSITION_PWM_CHANNEL_COUNT; channel++)
    {
        LL_SYSCFG_SetEXTISource(channel_config[channel].syscfg_port,
                                channel_config[channel].syscfg_line);
    }

    LL_EXTI_ClearFlag_0_31(POSITION_PWM_ALL_EXTI_LINES);
    LL_EXTI_EnableRisingTrig_0_31(POSITION_PWM_ALL_EXTI_LINES);
    LL_EXTI_EnableFallingTrig_0_31(POSITION_PWM_ALL_EXTI_LINES);
    LL_EXTI_EnableIT_0_31(POSITION_PWM_ALL_EXTI_LINES);

    NVIC_SetPriority(EXTI0_IRQn, 2U);
    NVIC_SetPriority(EXTI1_IRQn, 2U);
    NVIC_SetPriority(EXTI2_TSC_IRQn, 2U);
    NVIC_SetPriority(EXTI15_10_IRQn, 2U);
    NVIC_EnableIRQ(EXTI0_IRQn);
    NVIC_EnableIRQ(EXTI1_IRQn);
    NVIC_EnableIRQ(EXTI2_TSC_IRQn);
    NVIC_EnableIRQ(EXTI15_10_IRQn);
}

void position_pwm_init(void)
{
    uint8_t channel;

    for (channel = 0U; channel < POSITION_PWM_CHANNEL_COUNT; channel++)
    {
        have_rise[channel] = false;
        have_high[channel] = false;
        have_sample[channel] = false;
        new_sample[channel] = false;
        last_rise_tick[channel] = 0U;
        pending_high_us[channel] = 0U;
        latest_period_us[channel] = 0U;
        latest_high_us[channel] = 0U;
    }

    position_pwm_timer_init();
    position_pwm_gpio_exti_init();
}

bool position_pwm_get_latest_channel(uint8_t channel, position_pwm_sample_t *sample)
{
    uint32_t high_us;
    uint32_t period_us;

    if ((sample == NULL) || !position_pwm_channel_valid(channel))
    {
        return false;
    }

    position_pwm_disable_irqs();
    if (!have_sample[channel])
    {
        position_pwm_enable_irqs();
        return false;
    }

    high_us = latest_high_us[channel];
    period_us = latest_period_us[channel];
    position_pwm_enable_irqs();

    position_pwm_fill_sample(sample, high_us, period_us);
    return true;
}

bool position_pwm_take_new_sample_channel(uint8_t channel, position_pwm_sample_t *sample)
{
    uint32_t high_us;
    uint32_t period_us;

    if ((sample == NULL) || !position_pwm_channel_valid(channel))
    {
        return false;
    }

    position_pwm_disable_irqs();
    if (!new_sample[channel] || !have_sample[channel])
    {
        position_pwm_enable_irqs();
        return false;
    }

    high_us = latest_high_us[channel];
    period_us = latest_period_us[channel];
    new_sample[channel] = false;
    position_pwm_enable_irqs();

    position_pwm_fill_sample(sample, high_us, period_us);
    return true;
}

static void position_pwm_handle_edge(uint8_t channel)
{
    uint32_t now;
    bool level_high;

    if (!position_pwm_channel_valid(channel)
        || !LL_EXTI_IsActiveFlag_0_31(channel_config[channel].exti_line))
    {
        return;
    }

    LL_EXTI_ClearFlag_0_31(channel_config[channel].exti_line);
    now = LL_TIM_GetCounter(POSITION_PWM_TIMER);
    level_high = LL_GPIO_IsInputPinSet(channel_config[channel].gpio_port,
                                       channel_config[channel].gpio_pin);

    if (level_high)
    {
        if (have_rise[channel] && have_high[channel])
        {
            latest_period_us[channel] = now - last_rise_tick[channel];
            latest_high_us[channel] = pending_high_us[channel];
            have_sample[channel] = true;
            new_sample[channel] = true;
        }

        last_rise_tick[channel] = now;
        have_rise[channel] = true;
        have_high[channel] = false;
    }
    else if (have_rise[channel])
    {
        pending_high_us[channel] = now - last_rise_tick[channel];
        have_high[channel] = true;
    }
}

void EXTI0_IRQHandler(void)
{
    position_pwm_handle_edge(POSITION_PWM_CHANNEL_SIG1);
}

void EXTI1_IRQHandler(void)
{
    position_pwm_handle_edge(POSITION_PWM_CHANNEL_SIG3);
}

void EXTI2_TSC_IRQHandler(void)
{
    position_pwm_handle_edge(POSITION_PWM_CHANNEL_SIG2);
}

void EXTI15_10_IRQHandler(void)
{
    position_pwm_handle_edge(POSITION_PWM_CHANNEL_SIG4);
    position_pwm_handle_edge(POSITION_PWM_CHANNEL_SIG5);
}
