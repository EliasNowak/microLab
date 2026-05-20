#ifndef LOGAN_SPI_H
#define LOGAN_SPI_H

#include <stdbool.h>
#include <stdint.h>

#define LOGAN_SPI_CONTROL_SP1 (1U << 0)
#define LOGAN_SPI_CONTROL_SP2 (1U << 1)
#define LOGAN_SPI_CONTROL_SP3 (1U << 2)
#define LOGAN_SPI_CONTROL_SP4 (1U << 3)
#define LOGAN_SPI_CONTROL_SP5 (1U << 4)
#define LOGAN_SPI_CONTROL_DIR_FRONT (1U << 5)
#define LOGAN_SPI_CONTROL_FREEWHEEL (1U << 6)
#define LOGAN_SPI_CONTROL_LOCK_MASK 0x1FU

typedef enum
{
    LOGAN_SPI_OK = 0,
    LOGAN_SPI_NOT_READY,
    LOGAN_SPI_BUSY,
    LOGAN_SPI_RATE_LIMITED
} logan_spi_result_t;

typedef enum
{
    LOGAN_SPI_DIRECTION_BACK = 0,
    LOGAN_SPI_DIRECTION_FRONT
} logan_spi_direction_t;

void logan_spi_init(void);
void logan_spi_tick_1ms(void);

bool logan_spi_is_busy(void);

uint8_t logan_spi_make_control(bool freewheel,
                               logan_spi_direction_t direction,
                               uint8_t lock_mask);
logan_spi_result_t logan_spi_send_control(uint8_t control_byte);
logan_spi_result_t logan_spi_send_control_state(bool freewheel,
                                                logan_spi_direction_t direction,
                                                uint8_t lock_mask);

#endif /* LOGAN_SPI_H */
