/*
 * Motor ISR Implementation
 *
 * Fast interrupt-level UART2 handling for Shengyi DWG22 motor protocol.
 * Runs from TIM2 ISR every 5ms.
 */

#include "motor_isr.h"
#include "shengyi.h"
#include "../kernel/event.h"

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

/*
 * Motor protocol constants (frame: SOF, ID, CMD, LEN, payload, CKS, CR, LF)
 */
/* sizes come from shengyi.h */

/*
 * RX state machine states
 */
typedef enum {
    RX_WAIT_START = 0,      /* Waiting for 0x3A */
    RX_WAIT_SECOND,         /* Got 0x3A, waiting for next header byte */
    RX_WAIT_OPCODE,         /* Got header, waiting for opcode */
    RX_WAIT_LEN,            /* Got opcode, waiting for length */
    RX_READ_PAYLOAD,        /* Reading frame bytes */
} rx_state_t;

/*
 * Module state
 */
static struct {
    event_queue_t *evt_queue;       /* Output event queue */
    motor_isr_state_t state;        /* Protocol state */
    rx_state_t rx_state;            /* RX parser state */

    /* TX command buffer (tx_pending shared: main writes, ISR reads) */
    uint8_t tx_cmd[14];             /* Queued command frame */
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

    /* Timing */
    uint32_t rx_start_ms;           /* RX frame start time */
    uint32_t last_valid_rx_ms;      /* Last successful RX */
    uint8_t first_rx_seen;          /* Motor ready flag */

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
    g_motor_isr.rx_start_ms = 0;
    g_motor_isr.last_valid_rx_ms = 0;
    g_motor_isr.first_rx_seen = 0;

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
    while (uart_rx_available(UART2_BASE)) {
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
int motor_isr_queue_cmd(uint8_t assist_level,
                        bool light_on,
                        bool walk_active,
                        bool battery_low,
                        bool speed_over)
{
    /* Build 0x52 request frame */
    size_t len = shengyi_build_frame_0x52_req(
        assist_level,
        light_on ? 1u : 0u,
        walk_active ? 1u : 0u,
        battery_low ? 1u : 0u,
        speed_over ? 1u : 0u,
        g_motor_isr.tx_cmd,
        sizeof(g_motor_isr.tx_cmd)
    );

    if (len > 0 && len <= sizeof(g_motor_isr.tx_cmd)) {
        g_motor_isr.tx_len = (uint8_t)len;
        g_motor_isr.tx_pending = 1;
        return 1;
    }

    return 0;
}

/*
 * Queue a raw frame for transmission.
 */
int motor_isr_queue_frame(const uint8_t *frame, uint8_t len)
{
    if (!frame || len == 0 || len > sizeof(g_motor_isr.tx_cmd))
        return 0;

    for (uint8_t i = 0; i < len; ++i)
        g_motor_isr.tx_cmd[i] = frame[i];
    g_motor_isr.tx_len = len;
    g_motor_isr.tx_pending = 1u;
    return 1;
}

/*
 * Process incoming RX byte
 */
static void motor_isr_process_rx_byte(uint8_t byte, uint32_t now_ms)
{
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

    /* Valid frame received */
    g_motor_isr.stats.rx_count++;
    g_motor_isr.last_valid_rx_ms = now_ms;
    g_motor_isr.stats.last_rx_ms = now_ms;
    g_motor_isr.state = MOTOR_ISR_STATE_IDLE;

    /* Snapshot last frame for main-loop processing */
    if (g_motor_isr.rx_len <= SHENGYI_MAX_FRAME_SIZE) {
        for (uint8_t i = 0; i < g_motor_isr.rx_len; ++i)
            g_motor_isr.last_frame[i] = g_motor_isr.rx_buf[i];
        g_motor_isr.last_len = g_motor_isr.rx_len;
        g_motor_isr.last_opcode = g_motor_isr.rx_buf[2];
        g_motor_isr.last_seq++;
    }

    /* Post MOTOR_READY event on first successful RX */
    if (!g_motor_isr.first_rx_seen) {
        g_motor_isr.first_rx_seen = 1;
        motor_isr_post_event(EVT_MOTOR_READY, 0, now_ms);
    }

    /* Post MOTOR_STATE event with opcode in payload */
    uint8_t opcode = g_motor_isr.rx_buf[2];
    uint16_t payload = (uint16_t)opcode | ((uint16_t)g_motor_isr.last_seq << 8);
    motor_isr_post_event(EVT_MOTOR_STATE, payload, now_ms);
}

/*
 * Send TX command
 */
static void motor_isr_send_tx_cmd(void)
{
    if (!g_motor_isr.tx_pending || g_motor_isr.tx_len == 0)
        return;

    /* Send all bytes */
    for (uint8_t i = 0; i < g_motor_isr.tx_len; i++) {
        /* Wait for TX ready (should be immediate in ISR) */
        while (!uart_tx_ready(UART2_BASE))
            ;
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

int motor_isr_copy_last_frame(uint8_t *out, uint8_t cap, uint8_t *out_len,
                              uint8_t *out_op, uint8_t *out_seq)
{
    if (!out || cap == 0)
        return 0;

    uint8_t seq1;
    uint8_t seq2;
    uint8_t len;
    uint8_t op;

    do {
        seq1 = g_motor_isr.last_seq;
        MEMORY_BARRIER();  /* Ensure seq1 read before frame copy */
        len = g_motor_isr.last_len;
        op = g_motor_isr.last_opcode;
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

    return len ? 1 : 0;
}
