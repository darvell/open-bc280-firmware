/*
 * Button FSM - Gesture Recognition State Machine
 *
 * Recognizes button gestures from debounced input:
 *   - Short press: <800ms
 *   - Long press: ≥800ms
 *   - Combo press: Multiple buttons simultaneously
 *   - Hold-repeat: After 1200ms, repeat every 200ms
 *
 * Usage:
 *   1. Initialize: button_fsm_init(&fsm)
 *   2. Update: button_fsm_update(&fsm, buttons, now_ms) every cycle
 *   3. Poll: button_fsm_poll_event(&fsm, &evt) until returns false
 */

#ifndef INPUT_BUTTON_FSM_H
#define INPUT_BUTTON_FSM_H

#include <stdint.h>
#include <stdbool.h>
#include "src/kernel/event.h"

/*
 * Timing constants
 */
#define BTN_LONG_THRESHOLD_MS   800u   /* Short vs. long press */
#define BTN_REPEAT_START_MS     1200u  /* When to start repeating */
#define BTN_REPEAT_INTERVAL_MS  200u   /* Repeat rate */

/*
 * Button masks (match gpio_sampler)
 */
#define BTN_MASK_UP     0x01u
#define BTN_MASK_DOWN   0x02u
#define BTN_MASK_MENU   0x04u
#define BTN_MASK_POWER  0x08u

/*
 * FSM states
 */
typedef enum {
    BTN_STATE_IDLE = 0,         /* No buttons pressed */
    BTN_STATE_PRESSED,          /* Button(s) held, waiting for threshold */
    BTN_STATE_LONG_TRIGGERED,   /* Long press fired, waiting for release */
    BTN_STATE_REPEATING,        /* In repeat mode */
} button_state_t;

/*
 * Event buffer for FSM output
 */
#define BTN_EVENT_BUFFER_SIZE 4

typedef struct {
    event_t events[BTN_EVENT_BUFFER_SIZE];
    uint8_t head;  /* Next write position */
    uint8_t tail;  /* Next read position */
    uint8_t count; /* Number of pending events */
} button_event_buffer_t;

/*
 * Button FSM state
 */
typedef struct {
    button_state_t state;        /* Current FSM state */
    uint8_t buttons_pressed;     /* Current button state */
    uint8_t buttons_last;        /* Previous button state */
    uint32_t press_start_ms;     /* When current press started */
    uint32_t last_repeat_ms;     /* When last repeat fired */
    button_event_buffer_t buffer; /* Pending events */
} button_fsm_t;

/*
 * Initialize FSM to idle state
 */
void button_fsm_init(button_fsm_t *fsm);

/*
 * Update FSM with current button state
 *
 * Call this regularly (e.g., every main loop iteration) with the
 * debounced button state from gpio_sampler.
 *
 * Arguments:
 *   fsm - FSM state
 *   buttons - Current debounced button state (bits 0-3)
 *   now_ms - Current timestamp in milliseconds
 */
void button_fsm_update(button_fsm_t *fsm, uint8_t buttons, uint32_t now_ms);

/*
 * Poll for next pending event
 *
 * Call repeatedly until returns false to drain all events.
 *
 * Arguments:
 *   fsm - FSM state
 *   evt - Output event (only valid if returns true)
 *
 * Returns:
 *   true if event was retrieved, false if no events pending
 */
bool button_fsm_poll_event(button_fsm_t *fsm, event_t *evt);

#endif /* INPUT_BUTTON_FSM_H */
