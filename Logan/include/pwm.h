#ifndef PWM_H
#define PWM_H

#include <stdbool.h>
#include <stdint.h>

#define STEP_PWM_DEFAULT_FREQUENCY_HZ 125U

void step_pwm_init(void);
bool step_pwm_set_frequency_hz(uint16_t frequency_hz);
void step_pwm_start(void);
void step_pwm_stop(void);

#endif /* PWM_H */
