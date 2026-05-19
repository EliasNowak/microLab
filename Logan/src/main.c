#include "main.h"
#include "pwm.h"
#include "spi.h"
#include "uart_cli.h"

#define APP_ALL_LOCKS LOGAN_SPI_CONTROL_LOCK_MASK

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
    UART_COMMAND_STEP_PWM_TEST = 'p'
} uart_command_t;

static uint8_t app_lock_mask = APP_ALL_LOCKS;
static logan_spi_direction_t app_direction = LOGAN_SPI_DIRECTION_BACK;
static bool app_freewheel = true;
static bool app_control_send_pending = true;
static bool app_step_start_pending = false;

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
        default:
            return UART_COMMAND_INVALID;
    }
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
    uart_cli_write_string("cmd: 1-5 select, F front, B back, s stop, l lock all, p step test\r\n");
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
            app_set_single_open_lock(LOGAN_SPI_CONTROL_SP1);
            break;
        case UART_COMMAND_SELECT_SP2:
            app_set_single_open_lock(LOGAN_SPI_CONTROL_SP2);
            break;
        case UART_COMMAND_SELECT_SP3:
            app_set_single_open_lock(LOGAN_SPI_CONTROL_SP3);
            break;
        case UART_COMMAND_SELECT_SP4:
            app_set_single_open_lock(LOGAN_SPI_CONTROL_SP4);
            break;
        case UART_COMMAND_SELECT_SP5:
            app_set_single_open_lock(LOGAN_SPI_CONTROL_SP5);
            break;
        case UART_COMMAND_LOCK_ALL:
            app_lock_all();
            uart_cli_write_string("locked\r\n");
            break;
        case UART_COMMAND_STOP:
            app_stop_drive();
            uart_cli_write_string("stopped\r\n");
            break;
        case UART_COMMAND_DRIVE_FRONT:
            app_start_drive(LOGAN_SPI_DIRECTION_FRONT);
            uart_cli_write_string("drive front\r\n");
            break;
        case UART_COMMAND_DRIVE_BACK:
            app_start_drive(LOGAN_SPI_DIRECTION_BACK);
            uart_cli_write_string("drive back\r\n");
            break;
        case UART_COMMAND_STEP_PWM_TEST:
            step_pwm_start();
            uart_cli_write_string("step test on\r\n");
            break;
        case UART_COMMAND_INVALID:
        default:
            uart_cli_write_string("invalid cmd, ? for help\r\n");
            break;
    }
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

int main(void)
{
    NVIC_SetPriorityGrouping(5U);
    SystemCoreClockUpdate();
    SysTick_Config(SystemCoreClock / 1000U);

    uart_cli_init();
    logan_spi_init();
    step_pwm_init();

    while (1)
    {
        bool did_work = false;
        uint8_t ch;

        if (uart_cli_read(&ch))
        {
            did_work = true;
            app_handle_uart_command(app_parse_uart_command(ch));
        }

        if (app_try_send_control())
        {
            did_work = true;
        }

        if (app_try_start_pending_step())
        {
            did_work = true;
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
}
