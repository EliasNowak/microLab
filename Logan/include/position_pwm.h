#ifndef POSITION_PWM_H
#define POSITION_PWM_H

#include <stdbool.h>
#include <stdint.h>

#define POSITION_PWM_EXPECTED_PERIOD_US 8000U
#define POSITION_PWM_MIN_PERIOD_US 7000U
#define POSITION_PWM_MAX_PERIOD_US 9000U
#define POSITION_PWM_DUTY_MAX 31U
#define POSITION_PWM_ERROR_DUTY 1U
#define POSITION_PWM_MIN_AREA_DUTY 2U
#define POSITION_PWM_MAX_AREA_DUTY 18U
#define POSITION_PWM_CHANNEL_COUNT 5U
#define POSITION_PWM_CHANNEL_SIG1 0U
#define POSITION_PWM_CHANNEL_SIG2 1U
#define POSITION_PWM_CHANNEL_SIG3 2U
#define POSITION_PWM_CHANNEL_SIG4 3U
#define POSITION_PWM_CHANNEL_SIG5 4U

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
bool position_pwm_has_sample(void);
bool position_pwm_has_sample_channel(uint8_t channel);
bool position_pwm_get_latest(position_pwm_sample_t *sample);
bool position_pwm_get_latest_channel(uint8_t channel, position_pwm_sample_t *sample);
bool position_pwm_take_new_sample(position_pwm_sample_t *sample);
bool position_pwm_take_new_sample_channel(uint8_t channel, position_pwm_sample_t *sample);

#endif /* POSITION_PWM_H */
