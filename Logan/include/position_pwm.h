#ifndef POSITION_PWM_H
#define POSITION_PWM_H

#include <stdbool.h>
#include <stdint.h>

#define POSITION_PWM_CHANNEL_COUNT 5U

typedef struct
{
    uint32_t period_us;
    uint32_t high_us;
    uint8_t duty_5bit;
    uint8_t area;
    bool valid;
    bool hardware_error;
} position_pwm_sample_t;

void position_pwm_init(void);
bool position_pwm_get_latest_channel(uint8_t channel, position_pwm_sample_t *sample);
bool position_pwm_take_new_sample_channel(uint8_t channel, position_pwm_sample_t *sample);

#endif /* POSITION_PWM_H */
