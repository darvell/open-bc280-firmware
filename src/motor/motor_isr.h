/*
 * Motor ISR Infrastructure
 *
 * Fast interrupt-level motor communication handler.
 * Called from TIM2 ISR every 5ms to manage Shengyi DWG22 UART protocol.
 *
 * Design:
 *   - Runs in ISR context (keep minimal!)
 *   - Manages UART2 RX/TX byte-level I/O
 *   - Posts events to queue when motor data arrives or errors occur
 *   - Handles TX timing (send command every 50ms)
 *   - Detects timeout if no response for >100ms
 *
 * Events posted:
 *   EVT_MOTOR_STATE   - New motor status received
 *   EVT_MOTOR_TIMEOUT - No response for >100ms
 *   EVT_MOTOR_ERROR   - Protocol error (bad checksum, etc)
 *   EVT_MOTOR_READY   - Motor controller ready (first response)
 */

#ifndef MOTOR_ISR_H
#define MOTOR_ISR_H

#include <stdint.h>
#include <stdbool.h>
#include "../kernel/event_queue.h"

/*
 * ISR timing parameters
 */
#define MOTOR_ISR_TICK_MS       5u      /* Called every 5ms from TIM2 */
#define MOTOR_TX_INTERVAL_MS    50u     /* Send command every 50ms */
#define MOTOR_RX_TIMEOUT_MS     100u    /* Timeout if no response */

/*
 * Protocol state machine states
 */
typedef enum {
    MOTOR_ISR_STATE_IDLE = 0,       /* Waiting to send command */
    MOTOR_ISR_STATE_TX_PENDING,     /* Command ready to send */
    MOTOR_ISR_STATE_WAIT_RESPONSE,  /* Waiting for motor reply */
    MOTOR_ISR_STATE_RX_ACTIVE,      /* Receiving response bytes */
    MOTOR_ISR_STATE_ERROR,          /* Protocol error state */
} motor_isr_state_t;

/*
 * Initialize motor ISR subsystem
 *
 * Args:
 *   evt_queue - Output event queue for posting motor events
 *
 * Note: Must be called before motor_isr_tick()
 */
void motor_isr_init(event_queue_t *evt_queue);

/*
 * Fast motor tick - called from TIM2 ISR every 5ms
 *
 * Responsibilities:
 *   - Poll UART2 RX for incoming bytes
 *   - Assemble Shengyi DWG22 protocol frames
 *   - Send TX command at 50ms intervals
 *   - Detect timeout/error conditions
 *   - Post events to queue when complete frames arrive
 *
 * Args:
 *   now_ms - Current millisecond timestamp
 *
 * Note: Keep this FAST - runs in ISR context!
 */
void motor_isr_tick(uint32_t now_ms);

/*
 * Queue a new motor command for transmission
 *
 * Args:
 *   assist_level - Mapped assist level (0x00-0xFF)
 *   light_on     - Headlight state (true = on)
 *   walk_active  - Walk assist state (true = active)
 *   battery_low  - Battery low flag (true = low)
 *   speed_over   - Speed limit exceeded (true = over)
 *
 * Note: Can be called from main loop. Command will be sent
 *       at next TX interval.
 */
int motor_isr_queue_cmd(uint8_t assist_level,
                         bool light_on,
                         bool walk_active,
                         bool battery_low,
                         bool speed_over);

/*
 * Queue a raw motor frame for transmission (e.g., 0xC1/0xC3 responses).
 *
 * Args:
 *   frame - full Shengyi frame bytes
 *   len   - frame length
 *
 * Returns 1 if queued, 0 otherwise.
 */
int motor_isr_queue_frame(const uint8_t *frame, uint8_t len);

/*
 * Copy the last received motor frame (if any).
 *
 * Args:
 *   out      - destination buffer
 *   cap      - size of destination buffer
 *   out_len  - returns frame length
 *   out_op   - returns opcode
 *   out_seq  - returns sequence id
 *
 * Returns 1 if a frame was copied, 0 otherwise.
 */
int motor_isr_copy_last_frame(uint8_t *out, uint8_t cap, uint8_t *out_len,
                              uint8_t *out_op, uint8_t *out_seq);

/*
 * Get current ISR state (for debugging)
 */
motor_isr_state_t motor_isr_get_state(void);

/*
 * Get statistics (for debugging/telemetry)
 */
typedef struct {
    uint32_t tx_count;       /* Total commands sent */
    uint32_t rx_count;       /* Total valid responses */
    uint32_t rx_errors;      /* Protocol errors */
    uint32_t timeouts;       /* RX timeout count */
    uint32_t queue_full;     /* Event queue full errors */
    uint32_t last_rx_ms;     /* Timestamp of last valid RX */
} motor_isr_stats_t;

void motor_isr_get_stats(motor_isr_stats_t *stats);

#endif /* MOTOR_ISR_H */
