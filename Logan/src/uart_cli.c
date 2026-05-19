#include "uart_cli.h"

#include "main.h"

#define UART_CLI_USART USART2
#define UART_CLI_GPIO_PORT GPIOA
#define UART_CLI_TX_PIN LL_GPIO_PIN_2
#define UART_CLI_RX_PIN LL_GPIO_PIN_3
#define UART_CLI_GPIO_AF LL_GPIO_AF_7
#define UART_CLI_BAUDRATE 115200U
#define UART_CLI_RX_BUF_SIZE 64U
#define UART_CLI_TX_BUF_SIZE 128U

static volatile uint16_t rx_head = 0U;
static volatile uint16_t rx_tail = 0U;
static uint8_t rx_buffer[UART_CLI_RX_BUF_SIZE];

static volatile uint16_t tx_head = 0U;
static volatile uint16_t tx_tail = 0U;
static uint8_t tx_buffer[UART_CLI_TX_BUF_SIZE];

static uint16_t uart_cli_next_index(uint16_t index, uint16_t size)
{
    index++;
    if (index >= size)
    {
        index = 0U;
    }
    return index;
}

bool uart_cli_is_ready(void)
{
    return LL_USART_IsActiveFlag_TEACK(UART_CLI_USART)
        && LL_USART_IsActiveFlag_REACK(UART_CLI_USART);
}

void uart_cli_init(void)
{
    LL_GPIO_InitTypeDef gpio_init;
    LL_USART_InitTypeDef usart_init;

    rx_head = 0U;
    rx_tail = 0U;
    tx_head = 0U;
    tx_tail = 0U;

    LL_AHB1_GRP1_EnableClock(LL_AHB1_GRP1_PERIPH_GPIOA);
    LL_APB1_GRP1_EnableClock(LL_APB1_GRP1_PERIPH_USART2);

    LL_GPIO_StructInit(&gpio_init);
    gpio_init.Pin = UART_CLI_TX_PIN | UART_CLI_RX_PIN;
    gpio_init.Mode = LL_GPIO_MODE_ALTERNATE;
    gpio_init.Speed = LL_GPIO_SPEED_FREQ_HIGH;
    gpio_init.OutputType = LL_GPIO_OUTPUT_PUSHPULL;
    gpio_init.Pull = LL_GPIO_PULL_UP;
    gpio_init.Alternate = UART_CLI_GPIO_AF;
    LL_GPIO_Init(UART_CLI_GPIO_PORT, &gpio_init);

    LL_USART_Disable(UART_CLI_USART);
    LL_USART_StructInit(&usart_init);
    usart_init.BaudRate = UART_CLI_BAUDRATE;
    usart_init.DataWidth = LL_USART_DATAWIDTH_8B;
    usart_init.StopBits = LL_USART_STOPBITS_1;
    usart_init.Parity = LL_USART_PARITY_NONE;
    usart_init.TransferDirection = LL_USART_DIRECTION_TX_RX;
    usart_init.HardwareFlowControl = LL_USART_HWCONTROL_NONE;
    usart_init.OverSampling = LL_USART_OVERSAMPLING_16;
    LL_USART_Init(UART_CLI_USART, &usart_init);

    LL_USART_Enable(UART_CLI_USART);
    LL_USART_EnableIT_RXNE(UART_CLI_USART);

    NVIC_SetPriority(USART2_IRQn, 0U);
    NVIC_EnableIRQ(USART2_IRQn);
}

bool uart_cli_read(uint8_t *ch)
{
    if ((ch == NULL) || !uart_cli_is_ready())
    {
        return false;
    }

    if (rx_head == rx_tail)
    {
        return false;
    }

    *ch = rx_buffer[rx_tail];
    rx_tail = uart_cli_next_index(rx_tail, UART_CLI_RX_BUF_SIZE);
    return true;
}

bool uart_cli_write_byte(uint8_t ch)
{
    uint16_t next_head;

    if (!uart_cli_is_ready())
    {
        return false;
    }

    next_head = uart_cli_next_index(tx_head, UART_CLI_TX_BUF_SIZE);
    if (next_head == tx_tail)
    {
        return false;
    }

    tx_buffer[tx_head] = ch;
    tx_head = next_head;
    LL_USART_EnableIT_TXE(UART_CLI_USART);
    return true;
}

size_t uart_cli_write(const uint8_t *data, size_t len)
{
    size_t written = 0U;

    if ((data == NULL) || !uart_cli_is_ready())
    {
        return 0U;
    }

    while (written < len)
    {
        uint16_t next_head = uart_cli_next_index(tx_head, UART_CLI_TX_BUF_SIZE);
        if (next_head == tx_tail)
        {
            break;
        }

        tx_buffer[tx_head] = data[written];
        tx_head = next_head;
        written++;
    }

    if (written > 0U)
    {
        LL_USART_EnableIT_TXE(UART_CLI_USART);
    }

    return written;
}

size_t uart_cli_write_string(const char *str)
{
    size_t written = 0U;

    if ((str == NULL) || !uart_cli_is_ready())
    {
        return 0U;
    }

    while (str[written] != '\0')
    {
        uint16_t next_head = uart_cli_next_index(tx_head, UART_CLI_TX_BUF_SIZE);
        if (next_head == tx_tail)
        {
            break;
        }

        tx_buffer[tx_head] = (uint8_t)str[written];
        tx_head = next_head;
        written++;
    }

    if (written > 0U)
    {
        LL_USART_EnableIT_TXE(UART_CLI_USART);
    }

    return written;
}

void USART2_IRQHandler(void)
{
    if (LL_USART_IsActiveFlag_RXNE(UART_CLI_USART))
    {
        uint8_t data = LL_USART_ReceiveData8(UART_CLI_USART);
        uint16_t next_head = uart_cli_next_index(rx_head, UART_CLI_RX_BUF_SIZE);
        if (next_head != rx_tail)
        {
            rx_buffer[rx_head] = data;
            rx_head = next_head;
        }
    }

    if (LL_USART_IsEnabledIT_TXE(UART_CLI_USART)
        && LL_USART_IsActiveFlag_TXE(UART_CLI_USART))
    {
        if (tx_tail != tx_head)
        {
            LL_USART_TransmitData8(UART_CLI_USART, tx_buffer[tx_tail]);
            tx_tail = uart_cli_next_index(tx_tail, UART_CLI_TX_BUF_SIZE);
        }
        else
        {
            LL_USART_DisableIT_TXE(UART_CLI_USART);
        }
    }
}
