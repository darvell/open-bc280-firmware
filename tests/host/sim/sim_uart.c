#include "sim_uart.h"
#include <stdio.h>
#include <string.h>

#define RX_CAP 2048
#define TX_CAP 4096

typedef struct {
    uint8_t rx[RX_CAP];
    size_t rx_head;
    size_t rx_tail;
    uint8_t tx[TX_CAP];
    size_t tx_len;
} sim_uart_t;

static sim_uart_t g_uart[SIM_UART_MAX];

void sim_uart_init(void)
{
    memset(g_uart, 0, sizeof(g_uart));
}

static sim_uart_t *port_uart(sim_uart_port_t port)
{
    if (port >= SIM_UART_MAX)
        return NULL;
    return &g_uart[port];
}

void sim_uart_rx_push(sim_uart_port_t port, const uint8_t *data, size_t len)
{
    sim_uart_t *u = port_uart(port);
    if (!u || !data)
        return;
    for (size_t i = 0; i < len; ++i)
    {
        size_t next = (u->rx_tail + 1u) % RX_CAP;
        if (next == u->rx_head)
            break;
        u->rx[u->rx_tail] = data[i];
        u->rx_tail = next;
    }
}

int sim_uart_rx_pop(sim_uart_port_t port, uint8_t *out)
{
    sim_uart_t *u = port_uart(port);
    if (!u || !out)
        return 0;
    if (u->rx_head == u->rx_tail)
        return 0;
    *out = u->rx[u->rx_head];
    u->rx_head = (u->rx_head + 1u) % RX_CAP;
    return 1;
}

void sim_uart_tx_write(sim_uart_port_t port, const uint8_t *data, size_t len)
{
    sim_uart_t *u = port_uart(port);
    if (!u || !data)
        return;
    size_t copy = len;
    if (copy > (TX_CAP - u->tx_len))
        copy = TX_CAP - u->tx_len;
    memcpy(&u->tx[u->tx_len], data, copy);
    u->tx_len += copy;

    (void)port;
}

size_t sim_uart_tx_size(sim_uart_port_t port)
{
    sim_uart_t *u = port_uart(port);
    if (!u)
        return 0;
    return u->tx_len;
}

size_t sim_uart_tx_read(sim_uart_port_t port, uint8_t *out, size_t max_len)
{
    sim_uart_t *u = port_uart(port);
    if (!u || !out || max_len == 0)
        return 0;
    size_t copy = u->tx_len;
    if (copy > max_len)
        copy = max_len;
    memcpy(out, u->tx, copy);
    u->tx_len = 0;
    return copy;
}
