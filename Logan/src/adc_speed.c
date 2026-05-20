#include "adc_speed.h"

#include "main.h"

#define ADC_SPEED_ADC ADC2
#define ADC_SPEED_GPIO_PORT GPIOA
#define ADC_SPEED_GPIO_PIN LL_GPIO_PIN_7
#define ADC_SPEED_CHANNEL LL_ADC_CHANNEL_4
#define ADC_SPEED_RAW_MAX 4095U
#define ADC_SPEED_MIN_FREQUENCY_HZ 125U
#define ADC_SPEED_MAX_FREQUENCY_HZ 5000U
#define ADC_SPEED_SAMPLE_INTERVAL_MS 20U
#define ADC_SPEED_REGULATOR_STARTUP_MS 1U

typedef enum
{
    ADC_SPEED_STATE_OFF = 0,
    ADC_SPEED_STATE_REGULATOR_STARTUP,
    ADC_SPEED_STATE_CALIBRATING,
    ADC_SPEED_STATE_ENABLING,
    ADC_SPEED_STATE_READY
} adc_speed_state_t;

static volatile adc_speed_state_t adc_state = ADC_SPEED_STATE_OFF;
static volatile bool adc_conversion_active = false;
static volatile bool adc_update_pending = false;
static volatile bool latest_sample_valid = false;
static volatile uint16_t latest_raw = 0U;
static volatile uint16_t latest_frequency_hz = ADC_SPEED_MIN_FREQUENCY_HZ;

static uint8_t adc_startup_ticks_remaining = 0U;
static uint8_t adc_sample_ticks_remaining = ADC_SPEED_SAMPLE_INTERVAL_MS;

static uint16_t adc_speed_map_raw_to_frequency(uint16_t raw)
{
    uint32_t range = ADC_SPEED_MAX_FREQUENCY_HZ - ADC_SPEED_MIN_FREQUENCY_HZ;
    uint32_t frequency = ADC_SPEED_MIN_FREQUENCY_HZ;

    frequency += ((uint32_t)raw * range) / ADC_SPEED_RAW_MAX;
    if (frequency > ADC_SPEED_MAX_FREQUENCY_HZ)
    {
        frequency = ADC_SPEED_MAX_FREQUENCY_HZ;
    }

    return (uint16_t)frequency;
}

static void adc_speed_configure_gpio(void)
{
    LL_GPIO_InitTypeDef gpio_init;

    LL_AHB1_GRP1_EnableClock(LL_AHB1_GRP1_PERIPH_GPIOA);

    LL_GPIO_StructInit(&gpio_init);
    gpio_init.Pin = ADC_SPEED_GPIO_PIN;
    gpio_init.Mode = LL_GPIO_MODE_ANALOG;
    gpio_init.Pull = LL_GPIO_PULL_NO;
    LL_GPIO_Init(ADC_SPEED_GPIO_PORT, &gpio_init);
}

static void adc_speed_configure_adc(void)
{
    LL_AHB1_GRP1_EnableClock(LL_AHB1_GRP1_PERIPH_ADC12);

    LL_ADC_SetCommonClock(ADC12_COMMON, LL_ADC_CLOCK_SYNC_PCLK_DIV4);

    LL_ADC_SetResolution(ADC_SPEED_ADC, LL_ADC_RESOLUTION_12B);
    LL_ADC_SetDataAlignment(ADC_SPEED_ADC, LL_ADC_DATA_ALIGN_RIGHT);
    LL_ADC_SetLowPowerMode(ADC_SPEED_ADC, LL_ADC_LP_MODE_NONE);
    LL_ADC_REG_SetTriggerSource(ADC_SPEED_ADC, LL_ADC_REG_TRIG_SOFTWARE);
    LL_ADC_REG_SetContinuousMode(ADC_SPEED_ADC, LL_ADC_REG_CONV_SINGLE);
    LL_ADC_REG_SetDMATransfer(ADC_SPEED_ADC, LL_ADC_REG_DMA_TRANSFER_NONE);
    LL_ADC_REG_SetOverrun(ADC_SPEED_ADC, LL_ADC_REG_OVR_DATA_OVERWRITTEN);
    LL_ADC_REG_SetSequencerLength(ADC_SPEED_ADC, LL_ADC_REG_SEQ_SCAN_DISABLE);
    LL_ADC_REG_SetSequencerRanks(ADC_SPEED_ADC, LL_ADC_REG_RANK_1, ADC_SPEED_CHANNEL);
    LL_ADC_SetChannelSingleDiff(ADC_SPEED_ADC, ADC_SPEED_CHANNEL, LL_ADC_SINGLE_ENDED);
    LL_ADC_SetChannelSamplingTime(ADC_SPEED_ADC, ADC_SPEED_CHANNEL, LL_ADC_SAMPLINGTIME_181CYCLES_5);

    LL_ADC_ClearFlag_ADRDY(ADC_SPEED_ADC);
    LL_ADC_ClearFlag_EOC(ADC_SPEED_ADC);
    LL_ADC_ClearFlag_EOS(ADC_SPEED_ADC);
    LL_ADC_ClearFlag_OVR(ADC_SPEED_ADC);
    LL_ADC_EnableIT_EOC(ADC_SPEED_ADC);
    LL_ADC_EnableIT_OVR(ADC_SPEED_ADC);

    NVIC_SetPriority(ADC1_2_IRQn, 3U);
    NVIC_EnableIRQ(ADC1_2_IRQn);
}

static void adc_speed_start_conversion(void)
{
    if ((adc_state != ADC_SPEED_STATE_READY)
        || adc_conversion_active
        || !LL_ADC_IsEnabled(ADC_SPEED_ADC)
        || LL_ADC_REG_IsConversionOngoing(ADC_SPEED_ADC))
    {
        return;
    }

    adc_conversion_active = true;
    LL_ADC_REG_StartConversion(ADC_SPEED_ADC);
}

void adc_speed_init(void)
{
    latest_raw = 0U;
    latest_frequency_hz = ADC_SPEED_MIN_FREQUENCY_HZ;
    adc_update_pending = false;
    latest_sample_valid = false;
    adc_conversion_active = false;
    adc_sample_ticks_remaining = ADC_SPEED_SAMPLE_INTERVAL_MS;

    adc_speed_configure_gpio();
    adc_speed_configure_adc();

    LL_ADC_EnableInternalRegulator(ADC_SPEED_ADC);
    adc_startup_ticks_remaining = ADC_SPEED_REGULATOR_STARTUP_MS;
    adc_state = ADC_SPEED_STATE_REGULATOR_STARTUP;
}

void adc_speed_tick_1ms(void)
{
    switch (adc_state)
    {
        case ADC_SPEED_STATE_REGULATOR_STARTUP:
            if (adc_startup_ticks_remaining > 0U)
            {
                adc_startup_ticks_remaining--;
                break;
            }
            LL_ADC_StartCalibration(ADC_SPEED_ADC, LL_ADC_SINGLE_ENDED);
            adc_state = ADC_SPEED_STATE_CALIBRATING;
            break;
        case ADC_SPEED_STATE_CALIBRATING:
            if (!LL_ADC_IsCalibrationOnGoing(ADC_SPEED_ADC))
            {
                LL_ADC_ClearFlag_ADRDY(ADC_SPEED_ADC);
                LL_ADC_Enable(ADC_SPEED_ADC);
                adc_state = ADC_SPEED_STATE_ENABLING;
            }
            break;
        case ADC_SPEED_STATE_ENABLING:
            if (LL_ADC_IsActiveFlag_ADRDY(ADC_SPEED_ADC))
            {
                adc_state = ADC_SPEED_STATE_READY;
                adc_sample_ticks_remaining = 0U;
            }
            break;
        case ADC_SPEED_STATE_READY:
            if (adc_sample_ticks_remaining > 0U)
            {
                adc_sample_ticks_remaining--;
                break;
            }
            adc_sample_ticks_remaining = ADC_SPEED_SAMPLE_INTERVAL_MS;
            adc_speed_start_conversion();
            break;
        case ADC_SPEED_STATE_OFF:
        default:
            break;
    }
}

bool adc_speed_get_latest(adc_speed_sample_t *sample)
{
    bool ready;

    if (sample == NULL)
    {
        return false;
    }

    NVIC_DisableIRQ(ADC1_2_IRQn);
    sample->raw = latest_raw;
    sample->frequency_hz = latest_frequency_hz;
    ready = ((adc_state == ADC_SPEED_STATE_READY) && latest_sample_valid);
    NVIC_EnableIRQ(ADC1_2_IRQn);

    return ready;
}

bool adc_speed_take_update(adc_speed_sample_t *sample)
{
    bool ready;

    if (sample == NULL)
    {
        return false;
    }

    NVIC_DisableIRQ(ADC1_2_IRQn);
    if (!adc_update_pending)
    {
        NVIC_EnableIRQ(ADC1_2_IRQn);
        return false;
    }

    sample->raw = latest_raw;
    sample->frequency_hz = latest_frequency_hz;
    ready = ((adc_state == ADC_SPEED_STATE_READY) && latest_sample_valid);
    adc_update_pending = false;
    NVIC_EnableIRQ(ADC1_2_IRQn);

    return ready;
}

void ADC1_2_IRQHandler(void)
{
    if (LL_ADC_IsActiveFlag_OVR(ADC_SPEED_ADC))
    {
        LL_ADC_ClearFlag_OVR(ADC_SPEED_ADC);
        adc_conversion_active = false;
    }

    if (LL_ADC_IsActiveFlag_EOC(ADC_SPEED_ADC) || LL_ADC_IsActiveFlag_EOS(ADC_SPEED_ADC))
    {
        uint16_t raw = LL_ADC_REG_ReadConversionData12(ADC_SPEED_ADC);

        latest_raw = raw;
        latest_frequency_hz = adc_speed_map_raw_to_frequency(raw);
        latest_sample_valid = true;
        adc_update_pending = true;
        adc_conversion_active = false;

        LL_ADC_ClearFlag_EOC(ADC_SPEED_ADC);
        LL_ADC_ClearFlag_EOS(ADC_SPEED_ADC);
    }
}
