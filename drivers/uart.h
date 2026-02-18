#ifndef OPEN_FIRMWARE_DRIVERS_UART_H
#define OPEN_FIRMWARE_DRIVERS_UART_H

#include <stddef.h>
#include <stdint.h>

void uart_init_basic(uint32_t base, uint32_t brr_div);
void uart_set_9bit(uint32_t base, uint8_t enable);
int uart_tx_ready(uint32_t base);
void uart_putc(uint32_t base, uint8_t c);
void uart_putc_9bit(uint32_t base, uint16_t value);
void uart_write(uint32_t base, const uint8_t *data, size_t len);
int uart_rx_available(uint32_t base);
uint8_t uart_getc(uint32_t base);
uint16_t uart_getc_9bit(uint32_t base);
void uart_isr_rx_drain(uint32_t base);

/* Reconfigure baud rate on a live UART (disables/re-enables UE). */
void uart_set_baud(uint32_t base, uint32_t brr_div);

#endif
