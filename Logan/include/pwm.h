#ifndef PWM_H
#define PWM_H

#include <stdbool.h>
#include <stdint.h>

#define STEP_PWM_DEFAULT_FREQUENCY_HZ 125U
#define STEP_PWM_MAX_FREQUENCY_HZ 5000U

void step_pwm_init(void);
bool step_pwm_set_frequency_hz(uint16_t frequency_hz);
uint16_t step_pwm_get_frequency_hz(void);
void step_pwm_start(void);
void step_pwm_stop(void);
bool step_pwm_is_running(void);

#endif /* PWM_H */
