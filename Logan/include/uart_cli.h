#ifndef UART_CLI_H
#define UART_CLI_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

void uart_cli_init(void);

bool uart_cli_read(uint8_t *ch);
bool uart_cli_write_byte(uint8_t ch);
size_t uart_cli_write_string(const char *str);

#endif /* UART_CLI_H */
