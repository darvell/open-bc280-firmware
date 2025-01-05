#include "comm.h"

#include "drivers/uart.h"
#include "platform/time.h"
#include "platform/hw.h"

typedef struct {
    uint32_t base;
    uint8_t buf[COMM_MAX_PAYLOAD + 8];
    uint8_t len;
    uint8_t active;
    uint32_t last_rx_ms;
} uart_port_t;

static uart_port_t g_ports[] = {
    { UART1_BASE, {0}, 0, 1, 0 }, /* BLE UART (OEM app) + default active */
    { UART2_BASE, {0}, 0, 0, 0 }, /* motor UART (Shengyi DWG22) */
    { UART4_BASE, {0}, 0, 0, 0 }, /* optional / alternate */
};

int g_last_rx_port = 0;
int g_comm_skip_uart2 = 0;

static uint8_t tx_buf[COMM_MAX_PAYLOAD + 8];

void uart_write_port(int port_idx, const uint8_t *data, size_t len)
{
    if (port_idx < 0 || (size_t)port_idx >= (sizeof(g_ports) / sizeof(g_ports[0])))
        return;
    uart_write(g_ports[port_idx].base, data, len);
}

void send_frame_port(int port_idx, uint8_t cmd, const uint8_t *payload, uint8_t len)
{
    size_t frame_len = comm_frame_build(tx_buf, sizeof(tx_buf), cmd, payload, len);
    if (!frame_len)
        return;
    uart_write_port(port_idx, tx_buf, frame_len);
}

void send_status(uint8_t cmd, uint8_t status)
{
    uint8_t p[1] = {status};
    send_frame_port(g_last_rx_port, cmd | 0x80, p, 1);
}

static void handle_frame(const uint8_t *frame, uint8_t len)
{
    if (len < 4)
        return;
    uint8_t cmd = frame[1];
    uint8_t plen = frame[2];
    uint8_t want = 0;
    uint8_t ok = comm_frame_validate(frame, len, &want);
    if (!ok)
        return;

    const uint8_t *p = &frame[3];
    if (!comm_handle_command(cmd, p, plen))
        send_status(cmd, 0xFF);
}

void poll_uart_rx_ports(void)
{
    size_t port_count = sizeof(g_ports) / sizeof(g_ports[0]);
    for (size_t pi = 0; pi < port_count; ++pi)
    {
        uart_port_t *p = &g_ports[pi];
        if (g_comm_skip_uart2 && p->base == UART2_BASE)
            continue;
        while (uart_rx_available(p->base))
        {
            uint8_t b = uart_getc(p->base);
            uint8_t frame_len = 0;
            comm_parse_result_t res = comm_parser_feed(p->buf, sizeof(p->buf), COMM_MAX_PAYLOAD,
                                                       &p->len, b, &frame_len);
            if (res == COMM_PARSE_FRAME)
            {
                g_last_rx_port = (int)pi;
                p->active = 1;
                p->last_rx_ms = g_ms;
                handle_frame(p->buf, frame_len);
            }
            (void)frame_len;
        }
        /* drop inactivity >15s */
        if (p->active && (g_ms - p->last_rx_ms) > 15000)
            p->active = 0;
    }
}
