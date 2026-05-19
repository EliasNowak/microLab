#include "main.h"
#include "adc_speed.h"
#include "motion_control.h"
#include "position_pwm.h"
#include "pwm.h"
#include "spi.h"
#include "uart_cli.h"

#define APP_ALL_LOCKS LOGAN_SPI_CONTROL_LOCK_MASK
#define APP_POSITION_REPORT_DIVIDER 16U
#define APP_ADC_REPORT_DIVIDER 10U
#define APP_ADC_SPEED_DEADBAND_HZ 25U

typedef enum
{
    UART_COMMAND_NONE = 0,
    UART_COMMAND_INVALID,
    UART_COMMAND_HELP = '?',
    UART_COMMAND_SELECT_SP1 = '1',
    UART_COMMAND_SELECT_SP2 = '2',
    UART_COMMAND_SELECT_SP3 = '3',
    UART_COMMAND_SELECT_SP4 = '4',
    UART_COMMAND_SELECT_SP5 = '5',
    UART_COMMAND_LOCK_ALL = 'l',
    UART_COMMAND_STOP = 's',
    UART_COMMAND_DRIVE_FRONT = 'F',
    UART_COMMAND_DRIVE_BACK = 'B',
    UART_COMMAND_STEP_PWM_TEST = 'p',
    UART_COMMAND_POSITION_READ = 'r',
    UART_COMMAND_POSITION_MONITOR = 'm',
    UART_COMMAND_ADC_READ = 'a',
    UART_COMMAND_ADC_MONITOR = 'v',
    UART_COMMAND_TARGET_DIRECTION_TOGGLE = 'x'
} uart_command_t;

typedef enum
{
    TARGET_PARSE_NONE = 0,
    TARGET_PARSE_PENDING,
    TARGET_PARSE_READY,
    TARGET_PARSE_INVALID
} target_parse_result_t;

static uint8_t app_lock_mask = APP_ALL_LOCKS;
static logan_spi_direction_t app_direction = LOGAN_SPI_DIRECTION_BACK;
static bool app_freewheel = true;
static bool app_control_send_pending = true;
static bool app_step_start_pending = false;
static bool app_position_monitor_enabled = false;
static bool app_adc_monitor_enabled = false;
static uint8_t app_adc_report_count = 0U;
static uint16_t app_step_frequency_hz = STEP_PWM_DEFAULT_FREQUENCY_HZ;
static bool app_target_parse_active = false;
static uint8_t app_target_parse_digits = 0U;
static uint8_t app_target_parse_buffer[3];
static uint8_t app_position_report_count[POSITION_PWM_CHANNEL_COUNT];

static void app_stop_drive(void);

static uart_command_t app_parse_uart_command(uint8_t ch)
{
    switch (ch)
    {
        case '\r':
        case '\n':
        case ' ':
            return UART_COMMAND_NONE;
        case '?':
            return UART_COMMAND_HELP;
        case '1':
            return UART_COMMAND_SELECT_SP1;
        case '2':
            return UART_COMMAND_SELECT_SP2;
        case '3':
            return UART_COMMAND_SELECT_SP3;
        case '4':
            return UART_COMMAND_SELECT_SP4;
        case '5':
            return UART_COMMAND_SELECT_SP5;
        case 'l':
            return UART_COMMAND_LOCK_ALL;
        case 'p':
            return UART_COMMAND_STEP_PWM_TEST;
        case 's':
            return UART_COMMAND_STOP;
        case 'F':
            return UART_COMMAND_DRIVE_FRONT;
        case 'B':
            return UART_COMMAND_DRIVE_BACK;
        case 'r':
            return UART_COMMAND_POSITION_READ;
        case 'm':
            return UART_COMMAND_POSITION_MONITOR;
        case 'a':
            return UART_COMMAND_ADC_READ;
        case 'v':
            return UART_COMMAND_ADC_MONITOR;
        case 'x':
            return UART_COMMAND_TARGET_DIRECTION_TOGGLE;
        default:
            return UART_COMMAND_INVALID;
    }
}

static bool app_target_parse_finish(uint8_t *drive_id, uint8_t *target_area)
{
    uint8_t drive = MOTION_CONTROL_MIN_DRIVE;
    uint8_t area = 0U;

    if (app_target_parse_digits == 1U)
    {
        area = app_target_parse_buffer[0];
    }
    else if (app_target_parse_digits == 2U)
    {
        area = (uint8_t)((app_target_parse_buffer[0] * 10U) + app_target_parse_buffer[1]);
    }
    else if (app_target_parse_digits == 3U)
    {
        drive = app_target_parse_buffer[0];
        area = (uint8_t)((app_target_parse_buffer[1] * 10U) + app_target_parse_buffer[2]);
    }

    app_target_parse_active = false;

    if ((app_target_parse_digits == 0U)
        || (drive < MOTION_CONTROL_MIN_DRIVE)
        || (drive > MOTION_CONTROL_MAX_DRIVE)
        || (area < MOTION_CONTROL_MIN_AREA)
        || (area > MOTION_CONTROL_MAX_AREA))
    {
        return false;
    }

    *drive_id = drive;
    *target_area = area;
    return true;
}

static target_parse_result_t app_parse_target_command(uint8_t ch,
                                                      uint8_t *drive_id,
                                                      uint8_t *target_area)
{
    if (!app_target_parse_active)
    {
        if ((ch == 'T') || (ch == 't'))
        {
            app_target_parse_active = true;
            app_target_parse_digits = 0U;
            return TARGET_PARSE_PENDING;
        }

        return TARGET_PARSE_NONE;
    }

    if ((ch >= '0') && (ch <= '9'))
    {
        if (app_target_parse_digits >= sizeof(app_target_parse_buffer))
        {
            app_target_parse_active = false;
            return TARGET_PARSE_INVALID;
        }

        app_target_parse_buffer[app_target_parse_digits] = (uint8_t)(ch - '0');
        app_target_parse_digits++;

        if (app_target_parse_digits >= sizeof(app_target_parse_buffer))
        {
            if (app_target_parse_finish(drive_id, target_area))
            {
                return TARGET_PARSE_READY;
            }

            return TARGET_PARSE_INVALID;
        }

        return TARGET_PARSE_PENDING;
    }

    app_target_parse_active = false;
    if (((ch == '\r') || (ch == '\n') || (ch == ' '))
        && app_target_parse_finish(drive_id, target_area))
    {
        return TARGET_PARSE_READY;
    }

    return TARGET_PARSE_INVALID;
}

static char app_hex_digit(uint8_t value)
{
    value &= 0x0FU;
    if (value < 10U)
    {
        return (char)('0' + value);
    }
    return (char)('A' + (value - 10U));
}

static void app_write_control_tx(uint8_t control_byte)
{
    char message[] = "tx 0x00\r\n";

    message[5] = app_hex_digit(control_byte >> 4);
    message[6] = app_hex_digit(control_byte);
    uart_cli_write_string(message);
}

static void app_write_u32(uint32_t value)
{
    char buffer[10];
    uint8_t count = 0U;

    if (value == 0U)
    {
        uart_cli_write_byte('0');
        return;
    }

    while ((value > 0U) && (count < sizeof(buffer)))
    {
        buffer[count] = (char)('0' + (value % 10U));
        value /= 10U;
        count++;
    }

    while (count > 0U)
    {
        count--;
        uart_cli_write_byte((uint8_t)buffer[count]);
    }
}

static void app_write_position_sample(uint8_t channel, const position_pwm_sample_t *sample)
{
    uart_cli_write_string("pos");
    app_write_u32((uint32_t)channel + 1U);
    uart_cli_write_string(" p=");
    app_write_u32(sample->period_us);
    uart_cli_write_string("us h=");
    app_write_u32(sample->high_us);
    uart_cli_write_string("us d=");
    app_write_u32(sample->duty_5bit);
    uart_cli_write_string(" area=");
    app_write_u32(sample->area);

    if (sample->valid)
    {
        uart_cli_write_string(" ok\r\n");
    }
    else if (sample->hardware_error)
    {
        uart_cli_write_string(" hw-error\r\n");
    }
    else
    {
        uart_cli_write_string(" invalid\r\n");
    }
}

static void app_write_adc_sample(const adc_speed_sample_t *sample)
{
    uart_cli_write_string("adc raw=");
    app_write_u32(sample->raw);
    uart_cli_write_string(" freq=");
    app_write_u32(sample->frequency_hz);
    uart_cli_write_string("Hz\r\n");
}

static void app_write_motion_status_prefix(const char *prefix,
                                           const motion_control_status_t *status)
{
    uart_cli_write_string(prefix);
    uart_cli_write_string(" drive=");
    app_write_u32(status->drive_id);
    uart_cli_write_string(" area=");
    if (status->has_current_area)
    {
        app_write_u32(status->current_area);
    }
    else
    {
        uart_cli_write_byte('?');
    }
    uart_cli_write_string(" target=");
    app_write_u32(status->target_area);
}

static void app_write_motion_error(motion_control_error_t error)
{
    switch (error)
    {
        case MOTION_CONTROL_ERROR_INVALID_TARGET:
            uart_cli_write_string("invalid-target");
            break;
        case MOTION_CONTROL_ERROR_INVALID_DRIVE:
            uart_cli_write_string("invalid-drive");
            break;
        case MOTION_CONTROL_ERROR_POSITION_TIMEOUT:
            uart_cli_write_string("pos-timeout");
            break;
        case MOTION_CONTROL_ERROR_POSITION_INVALID:
            uart_cli_write_string("pos-invalid");
            break;
        case MOTION_CONTROL_ERROR_HARDWARE:
            uart_cli_write_string("hw-error");
            break;
        case MOTION_CONTROL_ERROR_NO_PROGRESS:
            uart_cli_write_string("no-progress");
            break;
        case MOTION_CONTROL_ERROR_WRONG_DIRECTION:
            uart_cli_write_string("wrong-direction");
            break;
        case MOTION_CONTROL_ERROR_NONE:
        default:
            uart_cli_write_string("none");
            break;
    }
}

static void app_write_latest_position(void)
{
    position_pwm_sample_t sample;
    uint8_t channel;

    for (channel = 0U; channel < POSITION_PWM_CHANNEL_COUNT; channel++)
    {
        if (position_pwm_get_latest_channel(channel, &sample))
        {
            app_write_position_sample(channel, &sample);
        }
        else
        {
            uart_cli_write_string("pos");
            app_write_u32((uint32_t)channel + 1U);
            uart_cli_write_string(" no sample\r\n");
        }
    }
}

static void app_write_latest_adc(void)
{
    adc_speed_sample_t sample;

    if (adc_speed_get_latest(&sample))
    {
        app_write_adc_sample(&sample);
    }
    else
    {
        uart_cli_write_string("adc no sample\r\n");
    }
}

static void app_request_control_send(void)
{
    app_control_send_pending = true;
}

static void app_set_single_open_lock(uint8_t open_lock)
{
    app_lock_mask = APP_ALL_LOCKS & (uint8_t)~open_lock;
    app_request_control_send();
}

static void app_write_help(void)
{
    uart_cli_write_string("cmd: 1-5 select, F/B drive, s stop, l lock, p step, r pos, m pos mon, a adc, v adc mon, T105 target, x dir map\r\n");
}

static void app_reset_position_report_counts(void)
{
    uint8_t channel;

    for (channel = 0U; channel < POSITION_PWM_CHANNEL_COUNT; channel++)
    {
        app_position_report_count[channel] = 0U;
    }
}

static void app_abort_motion_if_active(void)
{
    if (motion_control_abort())
    {
        app_stop_drive();
        uart_cli_write_string("target aborted\r\n");
    }
}

static void app_start_drive(logan_spi_direction_t direction)
{
    step_pwm_stop();
    app_direction = direction;
    app_freewheel = false;
    app_step_start_pending = true;
    app_request_control_send();
}

static void app_stop_drive(void)
{
    step_pwm_stop();
    app_step_start_pending = false;
    app_freewheel = true;
    app_request_control_send();
}

static void app_lock_all(void)
{
    app_stop_drive();
    app_lock_mask = APP_ALL_LOCKS;
}

static void app_handle_uart_command(uart_command_t command)
{
    switch (command)
    {
        case UART_COMMAND_NONE:
            break;
        case UART_COMMAND_HELP:
            app_write_help();
            break;
        case UART_COMMAND_SELECT_SP1:
            app_abort_motion_if_active();
            app_set_single_open_lock(LOGAN_SPI_CONTROL_SP1);
            break;
        case UART_COMMAND_SELECT_SP2:
            app_abort_motion_if_active();
            app_set_single_open_lock(LOGAN_SPI_CONTROL_SP2);
            break;
        case UART_COMMAND_SELECT_SP3:
            app_abort_motion_if_active();
            app_set_single_open_lock(LOGAN_SPI_CONTROL_SP3);
            break;
        case UART_COMMAND_SELECT_SP4:
            app_abort_motion_if_active();
            app_set_single_open_lock(LOGAN_SPI_CONTROL_SP4);
            break;
        case UART_COMMAND_SELECT_SP5:
            app_abort_motion_if_active();
            app_set_single_open_lock(LOGAN_SPI_CONTROL_SP5);
            break;
        case UART_COMMAND_LOCK_ALL:
            app_abort_motion_if_active();
            app_lock_all();
            uart_cli_write_string("locked\r\n");
            break;
        case UART_COMMAND_STOP:
            app_abort_motion_if_active();
            app_stop_drive();
            uart_cli_write_string("stopped\r\n");
            break;
        case UART_COMMAND_DRIVE_FRONT:
            app_abort_motion_if_active();
            app_start_drive(LOGAN_SPI_DIRECTION_FRONT);
            uart_cli_write_string("drive front\r\n");
            break;
        case UART_COMMAND_DRIVE_BACK:
            app_abort_motion_if_active();
            app_start_drive(LOGAN_SPI_DIRECTION_BACK);
            uart_cli_write_string("drive back\r\n");
            break;
        case UART_COMMAND_STEP_PWM_TEST:
            app_abort_motion_if_active();
            step_pwm_start();
            uart_cli_write_string("step test on\r\n");
            break;
        case UART_COMMAND_POSITION_READ:
            app_write_latest_position();
            break;
        case UART_COMMAND_POSITION_MONITOR:
            app_position_monitor_enabled = !app_position_monitor_enabled;
            app_reset_position_report_counts();
            if (app_position_monitor_enabled)
            {
                uart_cli_write_string("pos monitor on\r\n");
            }
            else
            {
                uart_cli_write_string("pos monitor off\r\n");
            }
            break;
        case UART_COMMAND_ADC_READ:
            app_write_latest_adc();
            break;
        case UART_COMMAND_ADC_MONITOR:
            app_adc_monitor_enabled = !app_adc_monitor_enabled;
            app_adc_report_count = 0U;
            if (app_adc_monitor_enabled)
            {
                uart_cli_write_string("adc monitor on\r\n");
            }
            else
            {
                uart_cli_write_string("adc monitor off\r\n");
            }
            break;
        case UART_COMMAND_TARGET_DIRECTION_TOGGLE:
            app_abort_motion_if_active();
            motion_control_set_front_increases_area(!motion_control_get_front_increases_area());
            if (motion_control_get_front_increases_area())
            {
                uart_cli_write_string("target map: F increases area\r\n");
            }
            else
            {
                uart_cli_write_string("target map: F decreases area\r\n");
            }
            break;
        case UART_COMMAND_INVALID:
        default:
            uart_cli_write_string("invalid cmd, ? for help\r\n");
            break;
    }
}

static void app_handle_target_command(uint8_t drive_id, uint8_t target_area)
{
    app_abort_motion_if_active();
    step_pwm_stop();
    app_step_start_pending = false;

    if (!motion_control_start_target(drive_id, target_area))
    {
        uart_cli_write_string("target invalid\r\n");
        return;
    }

    uart_cli_write_string("target drive=");
    app_write_u32(drive_id);
    uart_cli_write_string(" area=");
    app_write_u32(target_area);
    uart_cli_write_string("\r\n");
}

static void app_handle_uart_input(uint8_t ch)
{
    uint8_t target_area = 0U;
    uint8_t drive_id = MOTION_CONTROL_MIN_DRIVE;

    switch (app_parse_target_command(ch, &drive_id, &target_area))
    {
        case TARGET_PARSE_READY:
            app_handle_target_command(drive_id, target_area);
            return;
        case TARGET_PARSE_PENDING:
            return;
        case TARGET_PARSE_INVALID:
            uart_cli_write_string("invalid target, use T105 or T17\r\n");
            return;
        case TARGET_PARSE_NONE:
        default:
            break;
    }

    app_handle_uart_command(app_parse_uart_command(ch));
}

static bool app_try_send_control(void)
{
    uint8_t control_byte;
    logan_spi_result_t result;

    if (!app_control_send_pending)
    {
        return false;
    }

    result = logan_spi_send_control_state(app_freewheel,
                                          app_direction,
                                          app_lock_mask);
    if ((result == LOGAN_SPI_BUSY) || (result == LOGAN_SPI_RATE_LIMITED))
    {
        return false;
    }

    if (result != LOGAN_SPI_OK)
    {
        app_control_send_pending = false;
        uart_cli_write_string("spi error\r\n");
        return true;
    }

    app_control_send_pending = false;
    control_byte = logan_spi_make_control(app_freewheel,
                                          app_direction,
                                          app_lock_mask);
    app_write_control_tx(control_byte);
    return true;
}

static bool app_try_apply_adc_speed(void)
{
    adc_speed_sample_t sample;
    bool frequency_changed = false;

    if (!adc_speed_take_update(&sample))
    {
        return false;
    }

    if (((sample.frequency_hz > app_step_frequency_hz)
         && ((sample.frequency_hz - app_step_frequency_hz) >= APP_ADC_SPEED_DEADBAND_HZ))
        || ((sample.frequency_hz < app_step_frequency_hz)
            && ((app_step_frequency_hz - sample.frequency_hz) >= APP_ADC_SPEED_DEADBAND_HZ)))
    {
        if (step_pwm_set_frequency_hz(sample.frequency_hz))
        {
            app_step_frequency_hz = sample.frequency_hz;
            frequency_changed = true;
        }
    }

    if (app_adc_monitor_enabled)
    {
        app_adc_report_count++;
        if ((app_adc_report_count >= APP_ADC_REPORT_DIVIDER) || frequency_changed)
        {
            app_adc_report_count = 0U;
            app_write_adc_sample(&sample);
        }
    }

    return true;
}

static void app_apply_motion_action(const motion_control_action_t *action)
{
    if (action->stop_step)
    {
        step_pwm_stop();
        app_step_start_pending = false;
    }

    if (action->update_control)
    {
        app_freewheel = action->freewheel;
        app_direction = action->direction;
        app_lock_mask = action->lock_mask;
        app_request_control_send();
    }

    if (action->start_step)
    {
        step_pwm_start();
    }
}

static void app_report_motion_action(const motion_control_action_t *action)
{
    motion_control_status_t status = motion_control_get_status();

    if (action->started)
    {
        app_write_motion_status_prefix("target run", &status);
        if (status.direction == LOGAN_SPI_DIRECTION_FRONT)
        {
            uart_cli_write_string(" dir=F\r\n");
        }
        else
        {
            uart_cli_write_string(" dir=B\r\n");
        }
    }

    if (action->start_step)
    {
        uart_cli_write_string("target step on\r\n");
    }

    if (action->completed)
    {
        app_write_motion_status_prefix("target reached", &status);
        uart_cli_write_string("\r\n");
    }

    if (action->failed)
    {
        app_write_motion_status_prefix("target error", &status);
        uart_cli_write_string(" ");
        app_write_motion_error(status.error);
        uart_cli_write_string("\r\n");
    }
}

static bool app_try_run_motion(const position_pwm_sample_t *position_samples,
                               const bool *position_sample_ready)
{
    motion_control_action_t action;
    motion_control_status_t status = motion_control_get_status();
    const position_pwm_sample_t *new_position_sample = NULL;
    bool control_ready = !app_control_send_pending && !logan_spi_is_busy();

    if ((status.drive_id >= MOTION_CONTROL_MIN_DRIVE)
        && (status.drive_id <= MOTION_CONTROL_MAX_DRIVE)
        && position_sample_ready[status.drive_id - 1U])
    {
        new_position_sample = &position_samples[status.drive_id - 1U];
    }

    if (!motion_control_process(new_position_sample, control_ready, &action))
    {
        return false;
    }

    app_apply_motion_action(&action);
    app_report_motion_action(&action);
    return true;
}

static bool app_try_start_pending_step(void)
{
    if (!app_step_start_pending)
    {
        return false;
    }

    if (app_control_send_pending || logan_spi_is_busy())
    {
        return false;
    }

    step_pwm_start();
    app_step_start_pending = false;
    uart_cli_write_string("step on\r\n");
    return true;
}

static bool app_try_report_position(uint8_t channel, const position_pwm_sample_t *sample)
{
    if (!app_position_monitor_enabled)
    {
        return false;
    }

    if (sample == NULL)
    {
        return false;
    }

    app_position_report_count[channel]++;
    if (app_position_report_count[channel] < APP_POSITION_REPORT_DIVIDER)
    {
        return true;
    }

    app_position_report_count[channel] = 0U;
    app_write_position_sample(channel, sample);
    return true;
}

int main(void)
{
    NVIC_SetPriorityGrouping(5U);
    SystemCoreClockUpdate();
    SysTick_Config(SystemCoreClock / 1000U);

    uart_cli_init();
    logan_spi_init();
    step_pwm_init();
    position_pwm_init();
    adc_speed_init();
    motion_control_init();

    while (1)
    {
        bool did_work = false;
        uint8_t ch;
        uint8_t channel;
        position_pwm_sample_t position_samples[POSITION_PWM_CHANNEL_COUNT];
        bool position_sample_ready[POSITION_PWM_CHANNEL_COUNT] = { false };

        if (uart_cli_read(&ch))
        {
            did_work = true;
            app_handle_uart_input(ch);
        }

        if (app_try_send_control())
        {
            did_work = true;
        }

        if (app_try_start_pending_step())
        {
            did_work = true;
        }

        if (app_try_apply_adc_speed())
        {
            did_work = true;
        }

        for (channel = 0U; channel < POSITION_PWM_CHANNEL_COUNT; channel++)
        {
            if (position_pwm_take_new_sample_channel(channel, &position_samples[channel]))
            {
                position_sample_ready[channel] = true;
            }
        }

        if (app_try_run_motion(position_samples, position_sample_ready))
        {
            did_work = true;
        }

        for (channel = 0U; channel < POSITION_PWM_CHANNEL_COUNT; channel++)
        {
            if (app_try_report_position(channel,
                                        position_sample_ready[channel] ? &position_samples[channel] : NULL))
            {
                did_work = true;
            }
        }

        if (!did_work)
        {
            __WFI();
        }
    }
}

void SysTick_Handler(void)
{
    logan_spi_tick_1ms();
    adc_speed_tick_1ms();
    motion_control_tick_1ms();
}
