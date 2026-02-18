#include "comm.h"

#include <string.h>

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
    { UART2_BASE, {0}, 0, 0, 0 }, /* motor UART (Shengyi DWG22), ISR-owned in app mode */
    { UART4_BASE, {0}, 0, 0, 0 }, /* optional / alternate */
};

int g_last_rx_port = 0;
/* Keep UART2 RX ownership in motor ISR path to avoid byte races with comm parser. */
int g_comm_skip_uart2 = 1;

static uint8_t tx_buf[COMM_MAX_PAYLOAD + 8];

/* BLE TTM text overlay on UART1.
 * The module emits ASCII status frames (TTM:...) before/after binary protocol
 * traffic. We filter those lines and let only framed binary bytes continue.
 */
#define TTM_TEXT_BUF_LEN 64u
#define TTM_MAC_STR_LEN  13u /* 12 hex chars + NUL */

static struct {
    uint8_t connected;          /* 1 if "TTM:CONNECTED" received */
    uint8_t mac_received;       /* 1 if MAC address parsed successfully */
    char mac_str[TTM_MAC_STR_LEN]; /* 12-char hex MAC, e.g. "001122334455" */

    /* Text accumulation buffer for TTM messages */
    char text_buf[TTM_TEXT_BUF_LEN];
    uint8_t text_pos;
    uint8_t in_text;            /* 1 if accumulating a text line (started with 'T') */
} g_ttm;

static int ttm_starts_with(const char *buf, const char *prefix)
{
    while (*prefix)
    {
        if (*buf != *prefix)
            return 0;
        buf++;
        prefix++;
    }
    return 1;
}

static void ttm_parse_line(void)
{
    if (g_ttm.text_pos < TTM_TEXT_BUF_LEN)
        g_ttm.text_buf[g_ttm.text_pos] = '\0';
    else
        g_ttm.text_buf[TTM_TEXT_BUF_LEN - 1u] = '\0';

    if (ttm_starts_with(g_ttm.text_buf, "TTM:CONNECTED"))
    {
        g_ttm.connected = 1u;
    }
    else if (ttm_starts_with(g_ttm.text_buf, "TTM:DISCONNECT"))
    {
        g_ttm.connected = 0u;
    }
    else if (ttm_starts_with(g_ttm.text_buf, "TTM:MAC-") && g_ttm.text_pos >= 20u)
    {
        const char *p = &g_ttm.text_buf[8]; /* skip "TTM:MAC-" */
        uint8_t mi = 0;
        for (uint8_t i = 0; i < 32u && mi < 12u; ++i)
        {
            char c = p[i];
            if (c == '\0' || c == '\r' || c == '\n')
                break;
            if (c == ':')
                continue;
            g_ttm.mac_str[mi++] = c;
        }
        g_ttm.mac_str[mi] = '\0';
        if (mi == 12u)
            g_ttm.mac_received = 1u;
    }
    /* Ignore "TTM:MAC-?" echo (OEM behavior). */
}

/* Return 1 when byte belongs to TTM text (skip binary parsing for this byte). */
static int ttm_filter_byte(uint8_t b)
{
    if (b == COMM_SOF)
    {
        g_ttm.in_text = 0u;
        g_ttm.text_pos = 0u;
        return 0;
    }

    if (g_ttm.in_text)
    {
        if (b == '\n' || b == '\r')
        {
            if (g_ttm.text_pos > 0u)
                ttm_parse_line();
            g_ttm.in_text = 0u;
            g_ttm.text_pos = 0u;
            return 1;
        }
        if (g_ttm.text_pos < TTM_TEXT_BUF_LEN - 1u)
        g_ttm.text_buf[g_ttm.text_pos++] = (char)b;
        return 1;
    }

    if (b == 'T')
    {
        g_ttm.in_text = 1u;
        g_ttm.text_pos = 0u;
        g_ttm.text_buf[g_ttm.text_pos++] = (char)b;
        return 1;
    }

    /* Not text and not 0x55: pass to binary parser. */
    return 0;
}

void ble_ttm_send_mac_query(void)
{
    static const uint8_t query[] = "TTM:MAC-?\r\n";
    for (size_t i = 0; i < sizeof(query) - 1u; ++i)
        uart_putc(UART1_BASE, query[i]);
}

uint8_t ble_ttm_is_connected(void)
{
    return g_ttm.connected;
}

const char *ble_ttm_get_mac(void)
{
    return g_ttm.mac_str;
}

/* ---------- end TTM ---------- */

void uart_write_port(int port_idx, const uint8_t *data, size_t len)
{
    if (port_idx < 0 || (size_t)port_idx >= (sizeof(g_ports) / sizeof(g_ports[0])))
        return;
    if (!data || len == 0u)
        return;

    /* Raw/binary-safe TX for framed protocol responses. */
    for (size_t i = 0; i < len; ++i)
        uart_putc(g_ports[port_idx].base, data[i]);
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
        uint16_t rx_budget = 128u;
        while (rx_budget-- && uart_rx_available(p->base))
        {
            uint8_t b = uart_getc(p->base);

            if (pi == PORT_BLE && p->len == 0u && ttm_filter_byte(b))
                continue;

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
