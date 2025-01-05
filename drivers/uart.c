#include "drivers/uart.h"

#include "platform/hw.h"
#include "platform/mmio.h"

#define UART_RX_BUF_LEN 128u
#define UART_RX_BUF_MASK (UART_RX_BUF_LEN - 1u)

typedef struct {
    uint8_t buf[UART_RX_BUF_LEN];
    volatile uint16_t head;
    volatile uint16_t tail;
} uart_rx_fifo_t;

typedef struct {
    uint32_t base;
    uart_rx_fifo_t rx;
} uart_port_state_t;

static uart_port_state_t g_uart_ports[] = {
    { UART1_BASE, {{0}, 0u, 0u} },
    { UART2_BASE, {{0}, 0u, 0u} },
    { UART4_BASE, {{0}, 0u, 0u} },
};

static int uart_port_index(uint32_t base)
{
    for (size_t i = 0; i < (sizeof(g_uart_ports) / sizeof(g_uart_ports[0])); ++i)
    {
        if (g_uart_ports[i].base == base)
            return (int)i;
    }
    return -1;
}

static int uart_rx_fifo_pop(int idx, uint8_t *out)
{
    uart_rx_fifo_t *rx = &g_uart_ports[idx].rx;
    if (rx->head == rx->tail)
        return 0;
    *out = rx->buf[rx->tail];
    rx->tail = (uint16_t)((rx->tail + 1u) & UART_RX_BUF_MASK);
    return 1;
}

static void uart_rx_fifo_push(int idx, uint8_t b)
{
    uart_rx_fifo_t *rx = &g_uart_ports[idx].rx;
    uint16_t next = (uint16_t)((rx->head + 1u) & UART_RX_BUF_MASK);
    if (next == rx->tail)
        return; /* drop on overflow */
    rx->buf[rx->head] = b;
    rx->head = next;
}

void uart_init_basic(uint32_t base, uint32_t brr_div)
{
    /* Match OEM init: clear CR2/CR3 masked bits, set TE/RE, RXNEIE, then UE. */
    uint32_t cr2 = mmio_read32(UART_CR2(base)) & 0xCFFFu;
    uint32_t cr3 = mmio_read32(UART_CR3(base)) & 0xFCFFu;
    mmio_write32(UART_CR2(base), cr2);
    mmio_write32(UART_CR3(base), cr3);

    mmio_write32(UART_BRR(base), brr_div);

    uint32_t cr1 = mmio_read32(UART_CR1(base));
    cr1 = (cr1 & 0xE9F3u) | 0x000Cu; /* TE + RE */
    cr1 |= 0x0020u; /* RXNEIE */
    cr1 |= 0x2000u; /* UE */
    mmio_write32(UART_CR1(base), cr1);
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
    int idx = uart_port_index(base);
    if (idx >= 0)
    {
        uart_rx_fifo_t *rx = &g_uart_ports[idx].rx;
        if (rx->head != rx->tail)
            return 1;
    }
    return (mmio_read32(UART_SR(base)) & (1u << 5)) != 0; /* RXNE */
}

uint8_t uart_getc(uint32_t base)
{
    int idx = uart_port_index(base);
    uint8_t b = 0;
    if (idx >= 0 && uart_rx_fifo_pop(idx, &b))
        return b;
    return (uint8_t)mmio_read32(UART_DR(base));
}

void uart_isr_rx_drain(uint32_t base)
{
    int idx = uart_port_index(base);
    if (idx < 0)
        return;
    while (mmio_read32(UART_SR(base)) & (1u << 5))
    {
        uint8_t b = (uint8_t)mmio_read32(UART_DR(base));
        uart_rx_fifo_push(idx, b);
    }
}
