#include "spi.h"

#include "main.h"

#define LOGAN_SPI_INSTANCE SPI1

#define LOGAN_SPI_SCK_PORT GPIOB
#define LOGAN_SPI_SCK_PIN LL_GPIO_PIN_3
#define LOGAN_SPI_MISO_PORT GPIOA
#define LOGAN_SPI_MISO_PIN LL_GPIO_PIN_6
#define LOGAN_SPI_MOSI_PORT GPIOB
#define LOGAN_SPI_MOSI_PIN LL_GPIO_PIN_5
#define LOGAN_SPI_NSS_PORT GPIOA
#define LOGAN_SPI_NSS_PIN LL_GPIO_PIN_15
#define LOGAN_SPI_GPIO_AF LL_GPIO_AF_5

#define LOGAN_SPI_TARGET_HZ 1000000U
#define LOGAN_SPI_CONTROL_INTERVAL_MS 4U

static volatile bool initialized = false;
static volatile bool transfer_active = false;
static volatile bool tx_loaded = false;
static volatile uint8_t pending_tx = 0U;
static volatile uint8_t control_cooldown_ms = 0U;

static void logan_spi_select(void)
{
    LL_GPIO_ResetOutputPin(LOGAN_SPI_NSS_PORT, LOGAN_SPI_NSS_PIN);
}

static void logan_spi_deselect(void)
{
    LL_GPIO_SetOutputPin(LOGAN_SPI_NSS_PORT, LOGAN_SPI_NSS_PIN);
}

static uint32_t logan_spi_prescaler_for_pclk(uint32_t pclk_hz)
{
    if (pclk_hz <= (LOGAN_SPI_TARGET_HZ * 2U))
    {
        return LL_SPI_BAUDRATEPRESCALER_DIV2;
    }
    if (pclk_hz <= (LOGAN_SPI_TARGET_HZ * 4U))
    {
        return LL_SPI_BAUDRATEPRESCALER_DIV4;
    }
    if (pclk_hz <= (LOGAN_SPI_TARGET_HZ * 8U))
    {
        return LL_SPI_BAUDRATEPRESCALER_DIV8;
    }
    if (pclk_hz <= (LOGAN_SPI_TARGET_HZ * 16U))
    {
        return LL_SPI_BAUDRATEPRESCALER_DIV16;
    }
    if (pclk_hz <= (LOGAN_SPI_TARGET_HZ * 32U))
    {
        return LL_SPI_BAUDRATEPRESCALER_DIV32;
    }
    if (pclk_hz <= (LOGAN_SPI_TARGET_HZ * 64U))
    {
        return LL_SPI_BAUDRATEPRESCALER_DIV64;
    }
    if (pclk_hz <= (LOGAN_SPI_TARGET_HZ * 128U))
    {
        return LL_SPI_BAUDRATEPRESCALER_DIV128;
    }
    return LL_SPI_BAUDRATEPRESCALER_DIV256;
}

static void logan_spi_gpio_init(void)
{
    LL_GPIO_InitTypeDef gpio_init;

    LL_AHB1_GRP1_EnableClock(LL_AHB1_GRP1_PERIPH_GPIOA);
    LL_AHB1_GRP1_EnableClock(LL_AHB1_GRP1_PERIPH_GPIOB);

    LL_GPIO_StructInit(&gpio_init);
    gpio_init.Pin = LOGAN_SPI_MISO_PIN;
    gpio_init.Mode = LL_GPIO_MODE_ALTERNATE;
    gpio_init.Speed = LL_GPIO_SPEED_FREQ_HIGH;
    gpio_init.OutputType = LL_GPIO_OUTPUT_PUSHPULL;
    gpio_init.Pull = LL_GPIO_PULL_NO;
    gpio_init.Alternate = LOGAN_SPI_GPIO_AF;
    LL_GPIO_Init(LOGAN_SPI_MISO_PORT, &gpio_init);

    gpio_init.Pin = LOGAN_SPI_SCK_PIN | LOGAN_SPI_MOSI_PIN;
    LL_GPIO_Init(LOGAN_SPI_SCK_PORT, &gpio_init);

    LL_GPIO_SetOutputPin(LOGAN_SPI_NSS_PORT, LOGAN_SPI_NSS_PIN);
    LL_GPIO_StructInit(&gpio_init);
    gpio_init.Pin = LOGAN_SPI_NSS_PIN;
    gpio_init.Mode = LL_GPIO_MODE_OUTPUT;
    gpio_init.Speed = LL_GPIO_SPEED_FREQ_HIGH;
    gpio_init.OutputType = LL_GPIO_OUTPUT_PUSHPULL;
    gpio_init.Pull = LL_GPIO_PULL_NO;
    LL_GPIO_Init(LOGAN_SPI_NSS_PORT, &gpio_init);
}

void logan_spi_init(void)
{
    LL_RCC_ClocksTypeDef clocks;
    LL_SPI_InitTypeDef spi_init;

    initialized = false;
    transfer_active = false;
    tx_loaded = false;
    control_cooldown_ms = 0U;

    logan_spi_gpio_init();

    LL_APB2_GRP1_EnableClock(LL_APB2_GRP1_PERIPH_SPI1);
    LL_SPI_Disable(LOGAN_SPI_INSTANCE);

    LL_RCC_GetSystemClocksFreq(&clocks);

    LL_SPI_StructInit(&spi_init);
    spi_init.TransferDirection = LL_SPI_FULL_DUPLEX;
    spi_init.Mode = LL_SPI_MODE_MASTER;
    spi_init.DataWidth = LL_SPI_DATAWIDTH_8BIT;
    spi_init.ClockPolarity = LL_SPI_POLARITY_LOW;
    spi_init.ClockPhase = LL_SPI_PHASE_1EDGE;
    spi_init.NSS = LL_SPI_NSS_SOFT;
    spi_init.BaudRate = logan_spi_prescaler_for_pclk(clocks.PCLK2_Frequency);
    spi_init.BitOrder = LL_SPI_MSB_FIRST;
    spi_init.CRCCalculation = LL_SPI_CRCCALCULATION_DISABLE;
    LL_SPI_Init(LOGAN_SPI_INSTANCE, &spi_init);

    LL_SPI_DisableIT_TXE(LOGAN_SPI_INSTANCE);
    LL_SPI_DisableIT_RXNE(LOGAN_SPI_INSTANCE);
    LL_SPI_DisableIT_ERR(LOGAN_SPI_INSTANCE);
    LL_SPI_Enable(LOGAN_SPI_INSTANCE);

    NVIC_SetPriority(SPI1_IRQn, 1U);
    NVIC_EnableIRQ(SPI1_IRQn);

    initialized = true;
}

void logan_spi_tick_1ms(void)
{
    if (control_cooldown_ms > 0U)
    {
        control_cooldown_ms--;
    }
}

static bool logan_spi_is_ready(void)
{
    return initialized && (LL_SPI_IsEnabled(LOGAN_SPI_INSTANCE) != 0U);
}

bool logan_spi_is_busy(void)
{
    return transfer_active;
}

uint8_t logan_spi_make_control(bool freewheel,
                               logan_spi_direction_t direction,
                               uint8_t lock_mask)
{
    uint8_t control = lock_mask & LOGAN_SPI_CONTROL_LOCK_MASK;

    if (direction == LOGAN_SPI_DIRECTION_FRONT)
    {
        control |= LOGAN_SPI_CONTROL_DIR_FRONT;
    }

    if (freewheel)
    {
        control |= LOGAN_SPI_CONTROL_FREEWHEEL;
    }

    return control;
}

static logan_spi_result_t logan_spi_transfer_byte(uint8_t tx_byte)
{
    if (!logan_spi_is_ready())
    {
        return LOGAN_SPI_NOT_READY;
    }

    NVIC_DisableIRQ(SPI1_IRQn);
    if (transfer_active)
    {
        NVIC_EnableIRQ(SPI1_IRQn);
        return LOGAN_SPI_BUSY;
    }

    pending_tx = tx_byte;
    tx_loaded = false;
    transfer_active = true;

    logan_spi_select();
    LL_SPI_EnableIT_ERR(LOGAN_SPI_INSTANCE);
    LL_SPI_EnableIT_RXNE(LOGAN_SPI_INSTANCE);
    LL_SPI_EnableIT_TXE(LOGAN_SPI_INSTANCE);
    NVIC_EnableIRQ(SPI1_IRQn);

    return LOGAN_SPI_OK;
}

logan_spi_result_t logan_spi_send_control(uint8_t control_byte)
{
    logan_spi_result_t result;

    if (control_cooldown_ms > 0U)
    {
        return LOGAN_SPI_RATE_LIMITED;
    }

    result = logan_spi_transfer_byte(control_byte);
    if (result == LOGAN_SPI_OK)
    {
        control_cooldown_ms = LOGAN_SPI_CONTROL_INTERVAL_MS;
    }

    return result;
}

logan_spi_result_t logan_spi_send_control_state(bool freewheel,
                                                logan_spi_direction_t direction,
                                                uint8_t lock_mask)
{
    return logan_spi_send_control(logan_spi_make_control(freewheel,
                                                         direction,
                                                         lock_mask));
}

void SPI1_IRQHandler(void)
{
    if (LL_SPI_IsActiveFlag_MODF(LOGAN_SPI_INSTANCE))
    {
        LL_SPI_DisableIT_TXE(LOGAN_SPI_INSTANCE);
        LL_SPI_DisableIT_RXNE(LOGAN_SPI_INSTANCE);
        LL_SPI_DisableIT_ERR(LOGAN_SPI_INSTANCE);
        logan_spi_deselect();
        transfer_active = false;
        tx_loaded = false;
        LL_SPI_ClearFlag_MODF(LOGAN_SPI_INSTANCE);
        LL_SPI_Enable(LOGAN_SPI_INSTANCE);
        return;
    }

    if (LL_SPI_IsActiveFlag_OVR(LOGAN_SPI_INSTANCE))
    {
        LL_SPI_ClearFlag_OVR(LOGAN_SPI_INSTANCE);
    }

    if (LL_SPI_IsActiveFlag_FRE(LOGAN_SPI_INSTANCE))
    {
        LL_SPI_ClearFlag_FRE(LOGAN_SPI_INSTANCE);
    }

    if (LL_SPI_IsEnabledIT_TXE(LOGAN_SPI_INSTANCE)
        && LL_SPI_IsActiveFlag_TXE(LOGAN_SPI_INSTANCE)
        && !tx_loaded)
    {
        LL_SPI_TransmitData8(LOGAN_SPI_INSTANCE, pending_tx);
        tx_loaded = true;
        LL_SPI_DisableIT_TXE(LOGAN_SPI_INSTANCE);
    }

    if (LL_SPI_IsEnabledIT_RXNE(LOGAN_SPI_INSTANCE)
        && LL_SPI_IsActiveFlag_RXNE(LOGAN_SPI_INSTANCE))
    {
        (void)LL_SPI_ReceiveData8(LOGAN_SPI_INSTANCE);
        transfer_active = false;
        tx_loaded = false;
        LL_SPI_DisableIT_RXNE(LOGAN_SPI_INSTANCE);
        LL_SPI_DisableIT_ERR(LOGAN_SPI_INSTANCE);
        logan_spi_deselect();
    }
}
