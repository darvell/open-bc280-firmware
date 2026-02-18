/*
 * Motor ISR Implementation
 *
 * Fast interrupt-level UART2 handling for Shengyi DWG22 motor protocol.
 * Runs from TIM2 ISR every 5ms.
 */

#include "motor_isr.h"
#include "shengyi.h"
#include "../kernel/event.h"
#include "../util/bool_to_u8.h"

#ifndef HOST_TEST
#include "../../drivers/uart.h"
#include "../../platform/hw.h"
#include "../../platform/mmio.h"
#define MEMORY_BARRIER() mmio_dmb()
#else
#define MEMORY_BARRIER() __asm__ volatile("" ::: "memory")
/* Host test stubs */
static int uart_rx_available(uint32_t base) { (void)base; return 0; }
static uint8_t uart_getc(uint32_t base) { (void)base; return 0; }
static int uart_tx_ready(uint32_t base) { (void)base; return 1; }
static void uart_putc(uint32_t base, uint8_t c) { (void)base; (void)c; }
#define UART2_BASE 0x40004400u
#endif

/* Bounded TX readiness wait in ISR context. */
#define MOTOR_ISR_TX_READY_SPINS 128u
#define MOTOR_ISR_V2_CHECKSUM_BIAS 32u

/* Opportunistic V2 frame sizes observed on some controller variants. */
#define MOTOR_ISR_V2_HEURISTIC_MIN_LEN 3u
#define MOTOR_ISR_V2_HEURISTIC_MAX_LEN 5u

/*
 * Motor protocol constants (frame: SOF, ID, CMD, LEN, payload, CKS, CR, LF)
 */
/* sizes come from shengyi.h */

/*
 * RX state machine states (per-protocol).
 */
typedef enum {
    RX_WAIT_START = 0,      /* Waiting for SOF */
    RX_WAIT_SECOND,         /* Shengyi: got 0x3A, waiting for next header byte */
    RX_WAIT_OPCODE,         /* Shengyi: got header, waiting for opcode */
    RX_WAIT_LEN,            /* Shengyi: got opcode, waiting for length */
    RX_READ_PAYLOAD,        /* Shengyi: reading rest of frame */
} rx_state_t;

typedef enum {
    RX02_WAIT_SOF = 0,      /* Waiting for 0x02 */
    RX02_WAIT_LEN,          /* Waiting for length byte */
    RX02_READ,              /* Reading until expected total length */
} rx02_state_t;

/*
 * Module state
 */
static struct {
    event_queue_t *evt_queue;       /* Output event queue */
    motor_isr_state_t state;        /* Protocol state */
    rx_state_t rx_state;            /* RX parser state */

    /* TX command buffer (tx_pending shared: main writes, ISR reads) */
    uint8_t tx_cmd[MOTOR_ISR_TX_MAX]; /* Queued command frame (raw bytes) */
    uint8_t tx_len;                 /* Command frame length */
    volatile uint8_t tx_pending;    /* Command ready to send */
    uint32_t tx_last_ms;            /* Last TX timestamp */

    /* RX frame buffer */
    uint8_t rx_buf[SHENGYI_MAX_FRAME_SIZE];
    uint8_t rx_len;                 /* Current RX length */
    uint8_t rx_expected;            /* Expected total length */

    /* Last valid frame snapshot (ISR writes, main reads via copy_last_frame) */
    volatile uint8_t last_frame[SHENGYI_MAX_FRAME_SIZE];
    volatile uint8_t last_len;
    volatile uint8_t last_opcode;
    volatile uint8_t last_seq;      /* Sequence number for safe copy */
    volatile motor_proto_t last_proto;
    volatile uint16_t last_aux16;   /* protocol-specific (e.g., msg_id) */

    /* Timing */
    uint32_t rx_start_ms;           /* RX frame start time */
    uint32_t last_valid_rx_ms;      /* Last successful RX */
    uint8_t first_rx_seen;          /* Motor ready flag */

    /* Additional protocol parsers (OEM variants). */
    struct {
        rx02_state_t st;
        uint8_t buf[64];
        uint8_t len;
        uint8_t expected;
    } rx02;

    struct {
        uint8_t active;
        uint8_t buf[32];
        uint8_t len;
    } rx_auth;

    struct {
        uint8_t buf[5];
        uint8_t pos;
        uint8_t expected;
        uint8_t active;
        uint16_t msg_id;
    } rx_v2;

    /* Statistics */
    motor_isr_stats_t stats;
} g_motor_isr;

/*
 * Forward declarations
 */
static void motor_isr_process_rx_byte(uint8_t byte, uint32_t now_ms);
static void motor_isr_process_frame(uint32_t now_ms);
static void motor_isr_send_tx_cmd(void);
static void motor_isr_post_event(uint8_t type, uint16_t payload, uint32_t timestamp);
static bool motor_isr_v2_checksum_ok(const uint8_t *frame, uint8_t len);
static void motor_isr_v2_heuristic_capture(uint8_t byte, uint32_t now_ms);
static bool motor_isr_tx_ready_wait(void);

static void motor_isr_rx02_reset(void)
{
    g_motor_isr.rx02.st = RX02_WAIT_SOF;
    g_motor_isr.rx02.len = 0u;
    g_motor_isr.rx02.expected = 0u;
}

static uint8_t motor_isr_xor8(const uint8_t *p, uint8_t n)
{
    uint8_t x = 0u;
    for (uint8_t i = 0; i < n; ++i)
        x ^= p[i];
    return x;
}

static void motor_isr_v2_reset(void)
{
    g_motor_isr.rx_v2.pos = 0u;
    g_motor_isr.rx_v2.expected = 0u;
    g_motor_isr.rx_v2.active = 0u;
    g_motor_isr.rx_v2.msg_id = 0u;
}

static void motor_isr_capture_frame(motor_proto_t proto,
                                   const uint8_t *frame,
                                   uint8_t len,
                                   uint8_t op,
                                   uint16_t aux16,
                                   uint32_t now_ms)
{
    if (!frame || len == 0u || len > SHENGYI_MAX_FRAME_SIZE)
        return;

    for (uint8_t i = 0; i < len; ++i)
        g_motor_isr.last_frame[i] = frame[i];
    g_motor_isr.last_len = len;
    g_motor_isr.last_opcode = op;
    g_motor_isr.last_proto = proto;
    g_motor_isr.last_aux16 = aux16;
    g_motor_isr.last_seq++;

    g_motor_isr.stats.rx_count++;
    g_motor_isr.last_valid_rx_ms = now_ms;
    g_motor_isr.stats.last_rx_ms = now_ms;
    g_motor_isr.state = MOTOR_ISR_STATE_IDLE;

    if (!g_motor_isr.first_rx_seen)
    {
        g_motor_isr.first_rx_seen = 1;
        motor_isr_post_event(EVT_MOTOR_READY, 0, now_ms);
    }

    /* Post frame event; payload: (proto<<8) | op */
    uint16_t payload = (uint16_t)op | ((uint16_t)proto << 8);
    motor_isr_post_event(EVT_MOTOR_STATE, payload, now_ms);
}

static bool motor_isr_v2_checksum_ok(const uint8_t *frame, uint8_t len)
{
    if (!frame || len < MOTOR_ISR_V2_HEURISTIC_MIN_LEN || len > MOTOR_ISR_V2_HEURISTIC_MAX_LEN)
        return false;

    /* OEM heuristics use a pair of additive checksum relations in adjacent bytes. */
    uint8_t data0 = frame[len - 3];
    uint8_t data1 = frame[len - 2];
    uint8_t expected = frame[len - 1];
    if ((uint8_t)(data0 + data1) == expected)
        return true;
    if ((uint8_t)(data0 + data1 + MOTOR_ISR_V2_CHECKSUM_BIAS) == expected)
        return true;
    return false;
}

static void motor_isr_v2_heuristic_capture(uint8_t byte, uint32_t now_ms)
{
    /* Maintain a small sliding window of the most recent bytes and try each plausible
     * short-length candidate. This keeps parser behavior tolerant across controller variants. */
    if (g_motor_isr.rx_v2.pos >= MOTOR_ISR_V2_HEURISTIC_MAX_LEN)
    {
        for (uint8_t i = 1u; i < MOTOR_ISR_V2_HEURISTIC_MAX_LEN; ++i)
            g_motor_isr.rx_v2.buf[i - 1u] = g_motor_isr.rx_v2.buf[i];
        g_motor_isr.rx_v2.pos = MOTOR_ISR_V2_HEURISTIC_MAX_LEN - 1u;
    }

    g_motor_isr.rx_v2.buf[g_motor_isr.rx_v2.pos++] = byte;

    for (uint8_t len = MOTOR_ISR_V2_HEURISTIC_MIN_LEN; len <= MOTOR_ISR_V2_HEURISTIC_MAX_LEN; ++len)
    {
        if (g_motor_isr.rx_v2.pos < len)
            continue;

        const uint8_t *frame = &g_motor_isr.rx_v2.buf[g_motor_isr.rx_v2.pos - len];
        if (!motor_isr_v2_checksum_ok(frame, len))
            continue;

        uint16_t msg_id = (uint16_t)((uint16_t)frame[0] << 8u) | (uint16_t)frame[1];
        motor_isr_capture_frame(MOTOR_PROTO_V2_FIXED,
                                frame,
                                len,
                                frame[1],
                                msg_id,
                                now_ms);
        g_motor_isr.rx_v2.pos = 0u;
        return;
    }
}

static bool motor_isr_tx_ready_wait(void)
{
    for (uint32_t spin = 0u; spin < MOTOR_ISR_TX_READY_SPINS; ++spin)
    {
        if (uart_tx_ready(UART2_BASE))
            return true;
    }
    return false;
}

/*
 * Initialize motor ISR subsystem
 */
void motor_isr_init(event_queue_t *evt_queue)
{
    g_motor_isr.evt_queue = evt_queue;
    g_motor_isr.state = MOTOR_ISR_STATE_IDLE;
    g_motor_isr.rx_state = RX_WAIT_START;
    g_motor_isr.tx_len = 0;
    g_motor_isr.tx_pending = 0;
    g_motor_isr.tx_last_ms = 0;
    g_motor_isr.rx_len = 0;
    g_motor_isr.rx_expected = 0;
    g_motor_isr.last_len = 0;
    g_motor_isr.last_opcode = 0;
    g_motor_isr.last_seq = 0;
    g_motor_isr.last_proto = MOTOR_PROTO_SHENGYI_3A1A;
    g_motor_isr.last_aux16 = 0;
    g_motor_isr.rx_start_ms = 0;
    g_motor_isr.last_valid_rx_ms = 0;
    g_motor_isr.first_rx_seen = 0;

    motor_isr_rx02_reset();
    g_motor_isr.rx_auth.active = 0u;
    g_motor_isr.rx_auth.len = 0u;
    motor_isr_v2_reset();

    /* Clear stats */
    g_motor_isr.stats.tx_count = 0;
    g_motor_isr.stats.rx_count = 0;
    g_motor_isr.stats.rx_errors = 0;
    g_motor_isr.stats.timeouts = 0;
    g_motor_isr.stats.queue_full = 0;
    g_motor_isr.stats.last_rx_ms = 0;
}

/*
 * Fast motor tick - called from TIM2 ISR every 5ms
 */
void motor_isr_tick(uint32_t now_ms)
{
    /* Process any incoming RX bytes */
    uint16_t rx_budget = 128u;
    while (rx_budget-- && uart_rx_available(UART2_BASE)) {
        uint8_t byte = uart_getc(UART2_BASE);
        motor_isr_process_rx_byte(byte, now_ms);
    }

    /* Check for RX timeout */
    if (g_motor_isr.state == MOTOR_ISR_STATE_WAIT_RESPONSE ||
        g_motor_isr.state == MOTOR_ISR_STATE_RX_ACTIVE) {
        uint32_t elapsed = now_ms - g_motor_isr.rx_start_ms;
        if (elapsed >= MOTOR_RX_TIMEOUT_MS) {
            /* Timeout - post event and reset */
            motor_isr_post_event(EVT_MOTOR_TIMEOUT, 0, now_ms);
            g_motor_isr.stats.timeouts++;
            g_motor_isr.state = MOTOR_ISR_STATE_IDLE;
            g_motor_isr.rx_state = RX_WAIT_START;
            g_motor_isr.rx_len = 0;
            /* Clear any request-aligned capture state. */
            motor_isr_v2_reset();
        }
    }

    /* TX timing: send command every 50ms */
    uint32_t since_tx = now_ms - g_motor_isr.tx_last_ms;
    if (since_tx >= MOTOR_TX_INTERVAL_MS) {
        if (g_motor_isr.tx_pending) {
            /* Send queued command */
            motor_isr_send_tx_cmd();
            g_motor_isr.tx_last_ms = now_ms;
            g_motor_isr.state = MOTOR_ISR_STATE_WAIT_RESPONSE;
            g_motor_isr.rx_start_ms = now_ms;
            g_motor_isr.rx_state = RX_WAIT_START;
            g_motor_isr.rx_len = 0;
        }
    }
}

/*
 * Queue a new motor command for transmission
 */
bool motor_isr_queue_cmd(uint8_t assist_level,
                         bool light_on,
                         bool walk_active,
                         bool battery_low,
                         bool speed_over)
{
    /* Build 0x52 request frame */
    size_t len = shengyi_build_frame_0x52_req(
        assist_level,
        bool_to_u8(light_on),
        bool_to_u8(walk_active),
        bool_to_u8(battery_low),
        bool_to_u8(speed_over),
        g_motor_isr.tx_cmd,
        sizeof(g_motor_isr.tx_cmd)
    );

    if (len > 0u && len <= sizeof(g_motor_isr.tx_cmd))
    {
        g_motor_isr.tx_len = (uint8_t)len;
        g_motor_isr.tx_pending = 1u;
        return true;
    }

    return false;
}

/*
 * Queue a raw frame for transmission.
 */
bool motor_isr_queue_frame(const uint8_t *frame, uint8_t len)
{
    if (!frame || len == 0u || len > sizeof(g_motor_isr.tx_cmd))
        return false;

    for (uint8_t i = 0; i < len; ++i)
        g_motor_isr.tx_cmd[i] = frame[i];
    g_motor_isr.tx_len = len;
    g_motor_isr.tx_pending = 1u;
    return true;
}

bool motor_isr_tx_busy(void)
{
    return g_motor_isr.tx_pending != 0u;
}

void motor_isr_v2_expect(uint16_t msg_id, uint8_t expected_total_len)
{
    /* Keep this ISR-friendly: just arm a small capture buffer. */
    if (expected_total_len == 0u || expected_total_len > sizeof(g_motor_isr.rx_v2.buf))
    {
        motor_isr_v2_reset();
        return;
    }
    g_motor_isr.rx_v2.msg_id = msg_id;
    g_motor_isr.rx_v2.expected = expected_total_len;
    g_motor_isr.rx_v2.pos = 0u;
    g_motor_isr.rx_v2.active = 1u;
}

/*
 * Process incoming RX byte
 */
static void motor_isr_process_rx_byte(uint8_t byte, uint32_t now_ms)
{
    /*
     * OEM variants observed in v2.5.1:
     * - 0x3A ... sum16 ... CRLF (Shengyi / "tongsheng" misnomer in IDA)
     * - 0x02 len ... xor (packetized, cmd in byte2, xor covers all but last)
     * - 0x46/0x53 ... xor ... CR (auth-like, xor excludes first byte)
     * - Short 5-byte replies with simple checksums (heuristic "v2")
     *
     * We parse all of these opportunistically. The main loop can decide which
     * one to trust (usually 0x3A frames on Shengyi DWG22).
     */

    /* v2: short request/response frames (OEM mode=2).
     * The OEM aligns RX length to the last request; we support both:
     * - deterministic capture when motor_isr_v2_expect() is armed
     * - opportunistic 5-byte heuristic when not armed */
    if (g_motor_isr.rx_v2.active)
    {
        g_motor_isr.rx_v2.buf[g_motor_isr.rx_v2.pos++] = byte;
        if (g_motor_isr.rx_v2.pos >= g_motor_isr.rx_v2.expected)
        {
            const uint8_t *f = g_motor_isr.rx_v2.buf;
            uint8_t n = g_motor_isr.rx_v2.expected;
            if (motor_isr_v2_checksum_ok(f, n))
            {
                uint16_t msg_id = g_motor_isr.rx_v2.msg_id ? g_motor_isr.rx_v2.msg_id
                                                          : (uint16_t)((uint16_t)f[0] << 8) | f[1];
                motor_isr_capture_frame(MOTOR_PROTO_V2_FIXED, f, n, f[1], msg_id, now_ms);
            }
            motor_isr_v2_reset();
        }
    }
    else
    {
        /* Opportunistic sliding-window capture for variants we don't have request
         * alignment for yet. */
        motor_isr_v2_heuristic_capture(byte, now_ms);
    }

    /* auth-like XOR+CR frames (SOF 0x46 or 0x53). */
    if (!g_motor_isr.rx_auth.active)
    {
        if (byte == 0x46u || byte == 0x53u)
        {
            g_motor_isr.rx_auth.active = 1u;
            g_motor_isr.rx_auth.len = 0u;
            g_motor_isr.rx_auth.buf[g_motor_isr.rx_auth.len++] = byte;
        }
    }
    else
    {
        if (g_motor_isr.rx_auth.len < sizeof(g_motor_isr.rx_auth.buf))
            g_motor_isr.rx_auth.buf[g_motor_isr.rx_auth.len++] = byte;
        else
        {
            g_motor_isr.rx_auth.active = 0u;
            g_motor_isr.rx_auth.len = 0u;
        }

        if (byte == 0x0Du && g_motor_isr.rx_auth.len >= 4u)
        {
            /* Layout (OEM): [0]=SOF, [... data ...], [len-2]=xor, [len-1]=CR */
            uint8_t len = g_motor_isr.rx_auth.len;
            uint8_t x = 0u;
            for (uint8_t i = 1u; i + 2u < len; ++i)
                x ^= g_motor_isr.rx_auth.buf[i];
            if (x == g_motor_isr.rx_auth.buf[len - 2u])
            {
                uint8_t op = g_motor_isr.rx_auth.buf[0];
                motor_isr_capture_frame(MOTOR_PROTO_AUTH_XOR_CR,
                                        g_motor_isr.rx_auth.buf,
                                        len,
                                        op,
                                        0u,
                                        now_ms);
            }
            g_motor_isr.rx_auth.active = 0u;
            g_motor_isr.rx_auth.len = 0u;
        }
    }

    /* 0x02 len ... xor frames */
    switch (g_motor_isr.rx02.st)
    {
        case RX02_WAIT_SOF:
            if (byte == 0x02u)
            {
                g_motor_isr.rx02.buf[0] = byte;
                g_motor_isr.rx02.len = 1u;
                g_motor_isr.rx02.st = RX02_WAIT_LEN;
            }
            break;
        case RX02_WAIT_LEN:
            g_motor_isr.rx02.buf[g_motor_isr.rx02.len++] = byte;
            g_motor_isr.rx02.expected = byte;
            if (g_motor_isr.rx02.expected < 4u || g_motor_isr.rx02.expected > sizeof(g_motor_isr.rx02.buf))
            {
                motor_isr_rx02_reset();
            }
            else
            {
                g_motor_isr.rx02.st = RX02_READ;
            }
            break;
        case RX02_READ:
            if (g_motor_isr.rx02.len < sizeof(g_motor_isr.rx02.buf))
                g_motor_isr.rx02.buf[g_motor_isr.rx02.len++] = byte;
            else
            {
                motor_isr_rx02_reset();
                break;
            }
            if (g_motor_isr.rx02.len >= g_motor_isr.rx02.expected)
            {
                uint8_t exp = g_motor_isr.rx02.expected;
                uint8_t x = motor_isr_xor8(g_motor_isr.rx02.buf, (uint8_t)(exp - 1u));
                if (x == g_motor_isr.rx02.buf[exp - 1u])
                {
                    uint8_t op = (exp >= 3u) ? g_motor_isr.rx02.buf[2] : 0u;
                    motor_isr_capture_frame(MOTOR_PROTO_STX02_XOR,
                                            g_motor_isr.rx02.buf,
                                            exp,
                                            op,
                                            0u,
                                            now_ms);
                }
                motor_isr_rx02_reset();
            }
            break;
    }

    switch (g_motor_isr.rx_state) {
        case RX_WAIT_START:
            if (byte == SHENGYI_FRAME_START) {
                g_motor_isr.rx_buf[0] = byte;
                g_motor_isr.rx_len = 1;
                g_motor_isr.rx_state = RX_WAIT_SECOND;
                g_motor_isr.state = MOTOR_ISR_STATE_RX_ACTIVE;
            }
            break;

        case RX_WAIT_SECOND:
            /* Accept any second header byte to match OEM parser behavior */
            g_motor_isr.rx_buf[1] = byte;
            g_motor_isr.rx_len = 2;
            g_motor_isr.rx_state = RX_WAIT_OPCODE;
            break;

        case RX_WAIT_OPCODE:
            g_motor_isr.rx_buf[2] = byte;
            g_motor_isr.rx_len = 3;
            g_motor_isr.rx_state = RX_WAIT_LEN;
            break;

        case RX_WAIT_LEN: {
            uint8_t payload_len = byte;
            g_motor_isr.rx_buf[3] = payload_len;
            g_motor_isr.rx_len = 4;
            if (payload_len > SHENGYI_MAX_PAYLOAD_LEN) {
                motor_isr_post_event(EVT_MOTOR_ERROR, 0xFE, now_ms);
                g_motor_isr.stats.rx_errors++;
                g_motor_isr.rx_state = RX_WAIT_START;
                g_motor_isr.rx_len = 0;
                break;
            }
            g_motor_isr.rx_expected = (uint8_t)(payload_len + 8u);
            g_motor_isr.rx_state = RX_READ_PAYLOAD;
            break;
        }

        case RX_READ_PAYLOAD:
            if (g_motor_isr.rx_len < SHENGYI_MAX_FRAME_SIZE) {
                g_motor_isr.rx_buf[g_motor_isr.rx_len++] = byte;

                /* Check if frame complete */
                if (g_motor_isr.rx_len >= g_motor_isr.rx_expected) {
                    motor_isr_process_frame(now_ms);
                    g_motor_isr.rx_state = RX_WAIT_START;
                    g_motor_isr.rx_len = 0;
                }
            } else {
                /* Buffer overflow - reset */
                motor_isr_post_event(EVT_MOTOR_ERROR, 0xFF, now_ms);
                g_motor_isr.stats.rx_errors++;
                g_motor_isr.rx_state = RX_WAIT_START;
                g_motor_isr.rx_len = 0;
            }
            break;
    }
}

/*
 * Process complete frame
 */
static void motor_isr_process_frame(uint32_t now_ms)
{
    /* Validate checksum */
    if (g_motor_isr.rx_len < 6) {
        /* Too short */
        motor_isr_post_event(EVT_MOTOR_ERROR, 0x01, now_ms);
        g_motor_isr.stats.rx_errors++;
        return;
    }

    /* Calculate checksum (sum bytes 1 to len-4) */
    uint16_t expected_cks = shengyi_checksum16(g_motor_isr.rx_buf, g_motor_isr.rx_len);
    uint16_t frame_cks = (uint16_t)(g_motor_isr.rx_buf[g_motor_isr.rx_len - 4]) |
                        ((uint16_t)(g_motor_isr.rx_buf[g_motor_isr.rx_len - 3]) << 8);

    if (expected_cks != frame_cks) {
        /* Checksum mismatch */
        motor_isr_post_event(EVT_MOTOR_ERROR, 0x02, now_ms);
        g_motor_isr.stats.rx_errors++;
        return;
    }

    /* Valid frame received (Shengyi 0x3A framing). */
    motor_isr_capture_frame(MOTOR_PROTO_SHENGYI_3A1A,
                            g_motor_isr.rx_buf,
                            g_motor_isr.rx_len,
                            g_motor_isr.rx_buf[2],
                            0u,
                            now_ms);
}

/*
 * Send TX command
 */
static void motor_isr_send_tx_cmd(void)
{
    if (!g_motor_isr.tx_pending || g_motor_isr.tx_len == 0)
        return;

    if (!motor_isr_tx_ready_wait())
        return;

    /* Send all bytes */
    for (uint8_t i = 0; i < g_motor_isr.tx_len; i++) {
        if (!motor_isr_tx_ready_wait())
        {
            g_motor_isr.tx_pending = 0u;
            g_motor_isr.tx_len = 0u;
            return;
        }
        uart_putc(UART2_BASE, g_motor_isr.tx_cmd[i]);
    }

    g_motor_isr.stats.tx_count++;
    g_motor_isr.tx_pending = 0;
}

/*
 * Post event to queue
 */
static void motor_isr_post_event(uint8_t type, uint16_t payload, uint32_t timestamp)
{
    if (!g_motor_isr.evt_queue)
        return;

    event_t evt = event_create(type, payload, timestamp);

    if (!event_queue_push(g_motor_isr.evt_queue, &evt)) {
        /* Queue full - increment error counter */
        g_motor_isr.stats.queue_full++;
    }
}

/*
 * Get current ISR state
 */
motor_isr_state_t motor_isr_get_state(void)
{
    return g_motor_isr.state;
}

/*
 * Get statistics
 */
void motor_isr_get_stats(motor_isr_stats_t *stats)
{
    if (stats) {
        *stats = g_motor_isr.stats;
    }
}

bool motor_isr_copy_last_frame(uint8_t *out, uint8_t cap, uint8_t *out_len,
                              uint8_t *out_op, uint8_t *out_seq,
                              motor_proto_t *out_proto,
                              uint16_t *out_aux16)
{
    if (!out || cap == 0)
        return false;

    uint8_t seq1;
    uint8_t seq2;
    uint8_t len;
    uint8_t op;
    motor_proto_t proto;
    uint16_t aux16;

    do {
        seq1 = g_motor_isr.last_seq;
        MEMORY_BARRIER();  /* Ensure seq1 read before frame copy */
        len = g_motor_isr.last_len;
        op = g_motor_isr.last_opcode;
        proto = g_motor_isr.last_proto;
        aux16 = g_motor_isr.last_aux16;
        if (len > cap)
            len = cap;
        for (uint8_t i = 0; i < len; ++i)
            out[i] = g_motor_isr.last_frame[i];
        MEMORY_BARRIER();  /* Ensure copy completes before seq2 read */
        seq2 = g_motor_isr.last_seq;
    } while (seq1 != seq2);

    if (out_len)
        *out_len = len;
    if (out_op)
        *out_op = op;
    if (out_seq)
        *out_seq = seq1;
    if (out_proto)
        *out_proto = proto;
    if (out_aux16)
        *out_aux16 = aux16;

    return len != 0u;
}
