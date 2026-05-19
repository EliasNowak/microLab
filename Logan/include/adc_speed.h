#ifndef ADC_SPEED_H
#define ADC_SPEED_H

#include <stdbool.h>
#include <stdint.h>

#define ADC_SPEED_RAW_MAX 4095U
#define ADC_SPEED_MIN_FREQUENCY_HZ 125U
#define ADC_SPEED_MAX_FREQUENCY_HZ 5000U
#define ADC_SPEED_SAMPLE_INTERVAL_MS 20U

typedef struct
{
    uint16_t raw;
    uint16_t frequency_hz;
    bool ready;
} adc_speed_sample_t;

void adc_speed_init(void);
void adc_speed_tick_1ms(void);
bool adc_speed_get_latest(adc_speed_sample_t *sample);
bool adc_speed_take_update(adc_speed_sample_t *sample);

#endif /* ADC_SPEED_H */
