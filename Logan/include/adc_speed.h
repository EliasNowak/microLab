#ifndef ADC_SPEED_H
#define ADC_SPEED_H

#include <stdbool.h>
#include <stdint.h>

typedef struct
{
    uint16_t raw;
    uint16_t frequency_hz;
} adc_speed_sample_t;

void adc_speed_init(void);
void adc_speed_tick_1ms(void);
bool adc_speed_get_latest(adc_speed_sample_t *sample);
bool adc_speed_take_update(adc_speed_sample_t *sample);

#endif /* ADC_SPEED_H */
