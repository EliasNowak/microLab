#ifndef MOTION_CONTROL_H
#define MOTION_CONTROL_H

#include <stdbool.h>
#include <stdint.h>

#include "position_pwm.h"
#include "spi.h"

#define MOTION_CONTROL_MIN_AREA 1U
#define MOTION_CONTROL_MAX_AREA 17U
#define MOTION_CONTROL_MIN_DRIVE 1U
#define MOTION_CONTROL_MAX_DRIVE 5U

typedef enum
{
    MOTION_CONTROL_STATE_IDLE = 0,
    MOTION_CONTROL_STATE_WAIT_POSITION,
    MOTION_CONTROL_STATE_WAIT_SPI_START,
    MOTION_CONTROL_STATE_RUNNING,
    MOTION_CONTROL_STATE_REACHED,
    MOTION_CONTROL_STATE_ERROR
} motion_control_state_t;

typedef enum
{
    MOTION_CONTROL_ERROR_NONE = 0,
    MOTION_CONTROL_ERROR_INVALID_DRIVE,
    MOTION_CONTROL_ERROR_INVALID_TARGET,
    MOTION_CONTROL_ERROR_POSITION_TIMEOUT,
    MOTION_CONTROL_ERROR_POSITION_INVALID,
    MOTION_CONTROL_ERROR_HARDWARE,
    MOTION_CONTROL_ERROR_NO_PROGRESS,
    MOTION_CONTROL_ERROR_WRONG_DIRECTION
} motion_control_error_t;

typedef struct
{
    bool update_control;
    bool start_step;
    bool stop_step;
    bool started;
    bool completed;
    bool failed;
    bool freewheel;
    logan_spi_direction_t direction;
    uint8_t lock_mask;
} motion_control_action_t;

typedef struct
{
    motion_control_state_t state;
    motion_control_error_t error;
    uint8_t drive_id;
    uint8_t target_area;
    uint8_t current_area;
    bool has_current_area;
    logan_spi_direction_t direction;
} motion_control_status_t;

void motion_control_init(void);
void motion_control_tick_1ms(void);
bool motion_control_start_target(uint8_t drive_id, uint8_t target_area);
bool motion_control_abort(void);
bool motion_control_is_active(void);
void motion_control_set_front_increases_area(bool front_increases);
bool motion_control_get_front_increases_area(void);
bool motion_control_process(const position_pwm_sample_t *new_sample,
                            bool control_ready,
                            motion_control_action_t *action);
motion_control_status_t motion_control_get_status(void);

#endif /* MOTION_CONTROL_H */
