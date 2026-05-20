#include "motion_control.h"

#include <stddef.h>

#define MOTION_CONTROL_HOLD_LOCK_MASK LOGAN_SPI_CONTROL_LOCK_MASK
#define MOTION_CONTROL_SAMPLE_TIMEOUT_MS 250U
#define MOTION_CONTROL_NO_PROGRESS_TIMEOUT_MS 5000U

static volatile motion_control_state_t state = MOTION_CONTROL_STATE_IDLE;
static volatile motion_control_error_t error = MOTION_CONTROL_ERROR_NONE;
static volatile uint8_t drive_id = MOTION_CONTROL_MIN_DRIVE;
static volatile uint8_t target_area = 0U;
static volatile uint8_t current_area = 0U;
static volatile bool has_current_area = false;
static volatile bool front_increases_area = true;
static volatile logan_spi_direction_t direction = LOGAN_SPI_DIRECTION_FRONT;
static volatile uint16_t ms_since_position_sample = 0U;
static volatile uint16_t ms_since_progress = 0U;

static uint8_t last_area = 0U;

static bool motion_control_drive_valid(uint8_t drive)
{
    return (drive >= MOTION_CONTROL_MIN_DRIVE) && (drive <= MOTION_CONTROL_MAX_DRIVE);
}

static uint8_t motion_control_lock_bit_for_drive(uint8_t drive)
{
    static const uint8_t lock_bits[MOTION_CONTROL_MAX_DRIVE] =
    {
        LOGAN_SPI_CONTROL_SP1,
        LOGAN_SPI_CONTROL_SP2,
        LOGAN_SPI_CONTROL_SP3,
        LOGAN_SPI_CONTROL_SP4,
        LOGAN_SPI_CONTROL_SP5
    };

    if (!motion_control_drive_valid(drive))
    {
        return 0U;
    }

    return lock_bits[drive - 1U];
}

static uint8_t motion_control_drive_lock_mask(void)
{
    return MOTION_CONTROL_HOLD_LOCK_MASK & (uint8_t)~motion_control_lock_bit_for_drive(drive_id);
}

static void motion_control_clear_action(motion_control_action_t *action)
{
    action->update_control = false;
    action->start_step = false;
    action->stop_step = false;
    action->started = false;
    action->completed = false;
    action->failed = false;
    action->freewheel = true;
    action->direction = direction;
    action->lock_mask = MOTION_CONTROL_HOLD_LOCK_MASK;
}

static void motion_control_reset_timers(void)
{
    ms_since_position_sample = 0U;
    ms_since_progress = 0U;
}

static void motion_control_set_hold_action(motion_control_action_t *action)
{
    action->stop_step = true;
    action->update_control = true;
    action->freewheel = true;
    action->direction = direction;
    action->lock_mask = MOTION_CONTROL_HOLD_LOCK_MASK;
}

static void motion_control_fail(motion_control_error_t reason,
                                motion_control_action_t *action)
{
    error = reason;
    state = MOTION_CONTROL_STATE_ERROR;
    motion_control_set_hold_action(action);
    action->failed = true;
}

static bool motion_control_direction_increases_area(logan_spi_direction_t drive_direction)
{
    if (drive_direction == LOGAN_SPI_DIRECTION_FRONT)
    {
        return front_increases_area;
    }

    return !front_increases_area;
}

static logan_spi_direction_t motion_control_direction_for_target(void)
{
    bool need_increase = (target_area > current_area);

    if (need_increase == front_increases_area)
    {
        return LOGAN_SPI_DIRECTION_FRONT;
    }

    return LOGAN_SPI_DIRECTION_BACK;
}

static bool motion_control_target_reached(void)
{
    bool increases = motion_control_direction_increases_area(direction);

    if (increases)
    {
        return current_area >= target_area;
    }

    return current_area <= target_area;
}

static bool motion_control_moved_wrong_way(void)
{
    bool increases;

    if (last_area == 0U)
    {
        return false;
    }

    increases = motion_control_direction_increases_area(direction);
    if (increases)
    {
        return current_area < last_area;
    }

    return current_area > last_area;
}

static void motion_control_prepare_drive(motion_control_action_t *action)
{
    direction = motion_control_direction_for_target();
    last_area = current_area;
    motion_control_reset_timers();

    action->stop_step = true;
    action->update_control = true;
    action->started = true;
    action->freewheel = false;
    action->direction = direction;
    action->lock_mask = motion_control_drive_lock_mask();
    state = MOTION_CONTROL_STATE_WAIT_SPI_START;
}

static bool motion_control_accept_sample(const position_pwm_sample_t *sample,
                                         motion_control_action_t *action)
{
    bool active = motion_control_is_active();

    if (sample == NULL)
    {
        return false;
    }

    if (sample->hardware_error)
    {
        if (active)
        {
            motion_control_fail(MOTION_CONTROL_ERROR_HARDWARE, action);
            return true;
        }

        return false;
    }

    if (!sample->valid)
    {
        if (active
            && ((state == MOTION_CONTROL_STATE_RUNNING)
                || (state == MOTION_CONTROL_STATE_WAIT_SPI_START)))
        {
            motion_control_fail(MOTION_CONTROL_ERROR_POSITION_INVALID, action);
            return true;
        }

        return false;
    }

    current_area = sample->area;
    has_current_area = true;
    ms_since_position_sample = 0U;
    return true;
}

void motion_control_init(void)
{
    state = MOTION_CONTROL_STATE_IDLE;
    error = MOTION_CONTROL_ERROR_NONE;
    drive_id = MOTION_CONTROL_MIN_DRIVE;
    target_area = 0U;
    current_area = 0U;
    has_current_area = false;
    front_increases_area = true;
    direction = LOGAN_SPI_DIRECTION_FRONT;
    last_area = 0U;
    motion_control_reset_timers();
}

void motion_control_tick_1ms(void)
{
    if ((state == MOTION_CONTROL_STATE_WAIT_POSITION)
        || (state == MOTION_CONTROL_STATE_WAIT_SPI_START)
        || (state == MOTION_CONTROL_STATE_RUNNING))
    {
        if (ms_since_position_sample < UINT16_MAX)
        {
            ms_since_position_sample++;
        }

        if (state == MOTION_CONTROL_STATE_RUNNING)
        {
            if (ms_since_progress < UINT16_MAX)
            {
                ms_since_progress++;
            }
        }
    }
}

bool motion_control_start_target(uint8_t drive, uint8_t area)
{
    if (!motion_control_drive_valid(drive))
    {
        error = MOTION_CONTROL_ERROR_INVALID_DRIVE;
        state = MOTION_CONTROL_STATE_ERROR;
        return false;
    }

    if ((area < MOTION_CONTROL_MIN_AREA) || (area > MOTION_CONTROL_MAX_AREA))
    {
        error = MOTION_CONTROL_ERROR_INVALID_TARGET;
        state = MOTION_CONTROL_STATE_ERROR;
        return false;
    }

    drive_id = drive;
    target_area = area;
    error = MOTION_CONTROL_ERROR_NONE;
    state = MOTION_CONTROL_STATE_WAIT_POSITION;
    last_area = 0U;
    motion_control_reset_timers();
    return true;
}

bool motion_control_abort(void)
{
    if ((state == MOTION_CONTROL_STATE_IDLE)
        || (state == MOTION_CONTROL_STATE_REACHED)
        || (state == MOTION_CONTROL_STATE_ERROR))
    {
        state = MOTION_CONTROL_STATE_IDLE;
        error = MOTION_CONTROL_ERROR_NONE;
        return false;
    }

    state = MOTION_CONTROL_STATE_IDLE;
    error = MOTION_CONTROL_ERROR_NONE;
    return true;
}

bool motion_control_is_active(void)
{
    return (state == MOTION_CONTROL_STATE_WAIT_POSITION)
        || (state == MOTION_CONTROL_STATE_WAIT_SPI_START)
        || (state == MOTION_CONTROL_STATE_RUNNING);
}

void motion_control_set_front_increases_area(bool front_increases)
{
    front_increases_area = front_increases;
}

bool motion_control_get_front_increases_area(void)
{
    return front_increases_area;
}

bool motion_control_process(const position_pwm_sample_t *new_sample,
                            bool control_ready,
                            motion_control_action_t *action)
{
    bool accepted_sample;

    if (action == NULL)
    {
        return false;
    }

    motion_control_clear_action(action);
    accepted_sample = motion_control_accept_sample(new_sample, action);
    if (action->failed)
    {
        return true;
    }

    switch (state)
    {
        case MOTION_CONTROL_STATE_WAIT_POSITION:
            if (accepted_sample && has_current_area)
            {
                if (current_area == target_area)
                {
                    motion_control_set_hold_action(action);
                    action->completed = true;
                    state = MOTION_CONTROL_STATE_REACHED;
                    return true;
                }

                motion_control_prepare_drive(action);
                return true;
            }

            if (ms_since_position_sample >= MOTION_CONTROL_SAMPLE_TIMEOUT_MS)
            {
                motion_control_fail(MOTION_CONTROL_ERROR_POSITION_TIMEOUT, action);
                return true;
            }
            break;
        case MOTION_CONTROL_STATE_WAIT_SPI_START:
            if (ms_since_position_sample >= MOTION_CONTROL_SAMPLE_TIMEOUT_MS)
            {
                motion_control_fail(MOTION_CONTROL_ERROR_POSITION_TIMEOUT, action);
                return true;
            }

            if (control_ready)
            {
                action->start_step = true;
                state = MOTION_CONTROL_STATE_RUNNING;
                return true;
            }
            break;
        case MOTION_CONTROL_STATE_RUNNING:
            if (accepted_sample)
            {
                if (motion_control_moved_wrong_way())
                {
                    motion_control_fail(MOTION_CONTROL_ERROR_WRONG_DIRECTION, action);
                    return true;
                }

                if (current_area != last_area)
                {
                    last_area = current_area;
                    ms_since_progress = 0U;
                }

                if (motion_control_target_reached())
                {
                    motion_control_set_hold_action(action);
                    action->completed = true;
                    state = MOTION_CONTROL_STATE_REACHED;
                    return true;
                }
            }

            if (ms_since_position_sample >= MOTION_CONTROL_SAMPLE_TIMEOUT_MS)
            {
                motion_control_fail(MOTION_CONTROL_ERROR_POSITION_TIMEOUT, action);
                return true;
            }

            if (ms_since_progress >= MOTION_CONTROL_NO_PROGRESS_TIMEOUT_MS)
            {
                motion_control_fail(MOTION_CONTROL_ERROR_NO_PROGRESS, action);
                return true;
            }
            break;
        case MOTION_CONTROL_STATE_IDLE:
        case MOTION_CONTROL_STATE_REACHED:
        case MOTION_CONTROL_STATE_ERROR:
        default:
            break;
    }

    return false;
}

motion_control_status_t motion_control_get_status(void)
{
    motion_control_status_t status;

    status.state = state;
    status.error = error;
    status.drive_id = drive_id;
    status.target_area = target_area;
    status.current_area = current_area;
    status.has_current_area = has_current_area;
    status.direction = direction;
    return status;
}
