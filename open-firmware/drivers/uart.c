#include "drivers/uart.h"

#include "platform/hw.h"
#include "platform/mmio.h"

void uart_init_basic(uint32_t base, uint32_t brr_div)
{
    /* CR1: UE=1, TE=1, RE=1. */
    mmio_write32(UART_BRR(base), brr_div);
    mmio_write32(UART_CR1(base), (1u << 13) | (1u << 3) | (1u << 2));
}

int uart_tx_ready(uint32_t base)
{
    return (mmio_read32(UART_SR(base)) & (1u << 7)) != 0; /* TXE */
}

void uart_putc(uint32_t base, uint8_t c)
{
    while (!uart_tx_ready(base))
        ;
    mmio_write32(UART_DR(base), c);
}

void uart_write(uint32_t base, const uint8_t *data, size_t len)
{
    if (!data || len == 0)
        return;
    for (size_t i = 0; i < len; i++)
    {
        if (data[i] == '\n')
            uart_putc(base, '\r');
        uart_putc(base, data[i]);
    }
}

int uart_rx_available(uint32_t base)
{
    return (mmio_read32(UART_SR(base)) & (1u << 5)) != 0; /* RXNE */
}

uint8_t uart_getc(uint32_t base)
{
    return (uint8_t)mmio_read32(UART_DR(base));
}

