/*
 * Button FSM Implementation
 *
 * State machine for recognizing button gestures from debounced input.
 * Generates semantic button events (short, long, combo, repeat).
 */

#include "button_fsm.h"
#include <string.h>

/* Forward declarations for internal helpers */
static void emit_event(button_fsm_t *fsm, uint8_t type, uint32_t timestamp);
static void handle_press(button_fsm_t *fsm, uint32_t now_ms);
static void handle_release(button_fsm_t *fsm, uint8_t released, uint32_t now_ms);
static uint8_t button_to_event_short(uint8_t button_mask);
static uint8_t button_to_event_long(uint8_t button_mask);
static uint8_t detect_combo(uint8_t buttons);

/*
 * Initialize FSM
 */
void button_fsm_init(button_fsm_t *fsm)
{
    memset(fsm, 0, sizeof(*fsm));
    fsm->state = BTN_STATE_IDLE;
}

/*
 * Update FSM with current button state
 */
void button_fsm_update(button_fsm_t *fsm, uint8_t buttons, uint32_t now_ms)
{
    buttons &= 0x0F;  /* Mask to 4 bits */

    /* Detect transitions */
    uint8_t pressed = (uint8_t)(buttons & ~fsm->buttons_last);   /* New presses */
    uint8_t released = (uint8_t)(~buttons & fsm->buttons_last);  /* New releases */

    /* Handle new presses */
    if (pressed) {
        handle_press(fsm, now_ms);
    }

    /* Handle releases */
    if (released) {
        handle_release(fsm, released, now_ms);
    }

    /* State machine updates - allow multi-step transitions in single update */
    bool continue_processing = true;
    while (continue_processing) {
        continue_processing = false;

        switch (fsm->state) {
        case BTN_STATE_IDLE:
            /* Nothing to do when idle */
            break;

        case BTN_STATE_PRESSED:
            /* Check if we've crossed long press threshold */
            if (buttons && (now_ms - fsm->press_start_ms >= BTN_LONG_THRESHOLD_MS)) {
                /* Check for combo first */
                uint8_t combo = detect_combo(buttons);
                if (combo) {
                    emit_event(fsm, combo, now_ms);
                    fsm->state = BTN_STATE_LONG_TRIGGERED;
                } else {
                    /* Single button long press */
                    uint8_t long_event = button_to_event_long(buttons);
                    if (long_event) {
                        emit_event(fsm, long_event, now_ms);
                        fsm->state = BTN_STATE_LONG_TRIGGERED;
                    }
                }
                /* Continue to check if we should immediately transition to REPEATING */
                continue_processing = true;
            }
            break;

        case BTN_STATE_LONG_TRIGGERED:
            /* Check if we should start repeating */
            if (buttons && (now_ms - fsm->press_start_ms >= BTN_REPEAT_START_MS)) {
                /* Transition to repeating state */
                fsm->last_repeat_ms = now_ms;
                fsm->state = BTN_STATE_REPEATING;
                /* Continue to check if we should immediately emit first repeat */
                continue_processing = true;
            }
            break;

        case BTN_STATE_REPEATING:
            /* Generate repeat events */
            if (buttons) {
                if (now_ms - fsm->last_repeat_ms >= BTN_REPEAT_INTERVAL_MS) {
                    /* Only UP and DOWN support repeat */
                    if (buttons == BTN_MASK_UP) {
                        emit_event(fsm, EVT_BTN_REPEAT_UP, now_ms);
                        fsm->last_repeat_ms = now_ms;
                    } else if (buttons == BTN_MASK_DOWN) {
                        emit_event(fsm, EVT_BTN_REPEAT_DOWN, now_ms);
                        fsm->last_repeat_ms = now_ms;
                    }
                }
            }
            break;
        }
    }

    /* Update state tracking */
    fsm->buttons_pressed = buttons;
    fsm->buttons_last = buttons;
}

/*
 * Poll for next event
 */
bool button_fsm_poll_event(button_fsm_t *fsm, event_t *evt)
{
    if (fsm->buffer.count == 0) {
        return false;
    }

    /* Dequeue event */
    *evt = fsm->buffer.events[fsm->buffer.tail];
    fsm->buffer.tail = (fsm->buffer.tail + 1) % BTN_EVENT_BUFFER_SIZE;
    fsm->buffer.count--;

    return true;
}

/* ================================================================
 * Internal Helpers
 * ================================================================ */

/*
 * Emit an event to the buffer
 */
static void emit_event(button_fsm_t *fsm, uint8_t type, uint32_t timestamp)
{
    if (fsm->buffer.count >= BTN_EVENT_BUFFER_SIZE) {
        /* Buffer full - drop oldest event */
        fsm->buffer.tail = (fsm->buffer.tail + 1) % BTN_EVENT_BUFFER_SIZE;
        fsm->buffer.count--;
    }

    event_t evt = event_simple(type, timestamp);
    fsm->buffer.events[fsm->buffer.head] = evt;
    fsm->buffer.head = (fsm->buffer.head + 1) % BTN_EVENT_BUFFER_SIZE;
    fsm->buffer.count++;
}

/*
 * Handle button press
 */
static void handle_press(button_fsm_t *fsm, uint32_t now_ms)
{
    if (fsm->state == BTN_STATE_IDLE) {
        /* First press - start timing */
        fsm->press_start_ms = now_ms;
        fsm->state = BTN_STATE_PRESSED;
    }
    /* If already in PRESSED state, additional button was added (combo) */
}

/*
 * Handle button release
 */
static void handle_release(button_fsm_t *fsm, uint8_t released, uint32_t now_ms)
{
    /* If we're in PRESSED state (not yet triggered long), generate short press */
    if (fsm->state == BTN_STATE_PRESSED) {
        uint32_t duration = now_ms - fsm->press_start_ms;
        if (duration < BTN_LONG_THRESHOLD_MS) {
            /* Check for combo on what was pressed before release */
            uint8_t buttons_before = (uint8_t)(fsm->buttons_last);
            uint8_t combo = detect_combo(buttons_before);
            if (combo) {
                emit_event(fsm, combo, now_ms);
            } else {
                /* Single button short press - emit for each released button */
                for (uint8_t bit = 0; bit < 4; bit++) {
                    uint8_t mask = (uint8_t)(1u << bit);
                    if (released & mask) {
                        uint8_t short_event = button_to_event_short(mask);
                        if (short_event) {
                            emit_event(fsm, short_event, now_ms);
                        }
                    }
                }
            }
        }
    }

    /* If all buttons released, return to idle */
    if (fsm->buttons_pressed == released) {
        fsm->state = BTN_STATE_IDLE;
    }
}

/*
 * Map button mask to short press event
 */
static uint8_t button_to_event_short(uint8_t button_mask)
{
    switch (button_mask) {
    case BTN_MASK_UP:    return EVT_BTN_SHORT_UP;
    case BTN_MASK_DOWN:  return EVT_BTN_SHORT_DOWN;
    case BTN_MASK_MENU:  return EVT_BTN_SHORT_MENU;
    case BTN_MASK_POWER: return EVT_BTN_SHORT_POWER;
    default:             return 0;
    }
}

/*
 * Map button mask to long press event
 */
static uint8_t button_to_event_long(uint8_t button_mask)
{
    switch (button_mask) {
    case BTN_MASK_UP:    return EVT_BTN_LONG_UP;
    case BTN_MASK_DOWN:  return EVT_BTN_LONG_DOWN;
    case BTN_MASK_MENU:  return EVT_BTN_LONG_MENU;
    case BTN_MASK_POWER: return EVT_BTN_LONG_POWER;
    default:             return 0;
    }
}

/*
 * Detect combo button presses
 */
static uint8_t detect_combo(uint8_t buttons)
{
    /* Check known combos */
    if ((buttons & (BTN_MASK_UP | BTN_MASK_DOWN)) == (BTN_MASK_UP | BTN_MASK_DOWN)) {
        return EVT_BTN_COMBO_UP_DOWN;  /* Walk assist */
    }
    if ((buttons & (BTN_MASK_UP | BTN_MASK_MENU)) == (BTN_MASK_UP | BTN_MASK_MENU)) {
        return EVT_BTN_COMBO_UP_MENU;
    }
    if ((buttons & (BTN_MASK_DOWN | BTN_MASK_MENU)) == (BTN_MASK_DOWN | BTN_MASK_MENU)) {
        return EVT_BTN_COMBO_DOWN_MENU;
    }
    return 0;  /* Not a combo */
}
