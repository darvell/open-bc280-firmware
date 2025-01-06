/*
 * Event Types for Event-Driven Architecture
 *
 * All events are 8 bytes for cache efficiency and power-of-2 queue sizing.
 * Events are categorized by subsystem for easy filtering and dispatch.
 */

#ifndef KERNEL_EVENT_H
#define KERNEL_EVENT_H

#include <stdint.h>

/*
 * Event categories - high nibble identifies subsystem
 */
typedef enum {
    EVT_CAT_NONE    = 0x00,
    EVT_CAT_BUTTON  = 0x10,   /* Button/input events */
    EVT_CAT_MOTOR   = 0x20,   /* Motor state updates */
    EVT_CAT_CONTROL = 0x30,   /* Control commands */
    EVT_CAT_UI      = 0x40,   /* UI commands */
    EVT_CAT_POWER   = 0x50,   /* Power management */
    EVT_CAT_BLE     = 0x60,   /* BLE communication */
} event_category_t;

/*
 * Button events - semantic button actions, not raw GPIO
 */
typedef enum {
    /* Short press (<800ms) */
    EVT_BTN_SHORT_UP    = 0x11,
    EVT_BTN_SHORT_DOWN  = 0x12,
    EVT_BTN_SHORT_MENU  = 0x13,
    EVT_BTN_SHORT_POWER = 0x14,

    /* Long press (≥800ms) */
    EVT_BTN_LONG_UP     = 0x15,
    EVT_BTN_LONG_DOWN   = 0x16,
    EVT_BTN_LONG_MENU   = 0x17,
    EVT_BTN_LONG_POWER  = 0x18,

    /* Combo presses */
    EVT_BTN_COMBO_UP_DOWN   = 0x19,  /* Walk assist */
    EVT_BTN_COMBO_UP_MENU   = 0x1A,
    EVT_BTN_COMBO_DOWN_MENU = 0x1B,

    /* Hold-repeat (after initial long press) */
    EVT_BTN_REPEAT_UP   = 0x1C,
    EVT_BTN_REPEAT_DOWN = 0x1D,
} button_event_type_t;

/*
 * Control commands - actions that affect motor/assist behavior
 */
typedef enum {
    CMD_CTRL_GEAR_UP        = 0x31,
    CMD_CTRL_GEAR_DOWN      = 0x32,
    CMD_CTRL_GEAR_SET       = 0x33,  /* payload = gear number */
    CMD_CTRL_CRUISE_TOGGLE  = 0x34,
    CMD_CTRL_CRUISE_SET     = 0x35,  /* payload = speed */
    CMD_CTRL_WALK_START     = 0x36,
    CMD_CTRL_WALK_STOP      = 0x37,
    CMD_CTRL_REGEN_TOGGLE   = 0x38,
    CMD_CTRL_PROFILE_NEXT   = 0x39,
    CMD_CTRL_PROFILE_SET    = 0x3A,  /* payload = profile id */
    CMD_CTRL_LIGHT_TOGGLE   = 0x3B,
} control_cmd_type_t;

/*
 * UI commands - actions that affect display/navigation
 */
typedef enum {
    CMD_UI_PAGE_NEXT    = 0x41,
    CMD_UI_PAGE_PREV    = 0x42,
    CMD_UI_PAGE_SET     = 0x43,  /* payload = page id */
    CMD_UI_FOCUS_NEXT   = 0x44,
    CMD_UI_FOCUS_PREV   = 0x45,
    CMD_UI_VALUE_INC    = 0x46,
    CMD_UI_VALUE_DEC    = 0x47,
    CMD_UI_CONFIRM      = 0x48,
    CMD_UI_CANCEL       = 0x49,
    CMD_UI_MENU_ENTER   = 0x4A,
    CMD_UI_MENU_EXIT    = 0x4B,
    CMD_UI_REFRESH      = 0x4C,  /* Force redraw */
} ui_cmd_type_t;

/*
 * Motor state events - updates from motor controller
 */
typedef enum {
    EVT_MOTOR_STATE     = 0x21,  /* General state update */
    EVT_MOTOR_ERROR     = 0x22,  /* Error condition */
    EVT_MOTOR_READY     = 0x23,  /* Controller ready */
    EVT_MOTOR_TIMEOUT   = 0x24,  /* Communication timeout */
} motor_event_type_t;

/*
 * Unified event structure - 8 bytes for cache efficiency
 *
 * Layout:
 *   [0]    type      - event type (includes category in high nibble)
 *   [1]    flags     - event-specific flags
 *   [2-3]  payload16 - 16-bit payload (or 2x 8-bit)
 *   [4-7]  timestamp - millisecond timestamp (or 32-bit payload)
 */
typedef struct {
    uint8_t  type;       /* Event type (includes category) */
    uint8_t  flags;      /* Event-specific flags */
    uint16_t payload16;  /* 16-bit payload */
    uint32_t timestamp;  /* Timestamp or extended payload */
} event_t;

/* Static assert for size */
_Static_assert(sizeof(event_t) == 8, "event_t must be 8 bytes");

/*
 * Helper macros for event construction
 */
#define EVENT_CATEGORY(evt)     ((evt).type & 0xF0)
#define EVENT_IS_BUTTON(evt)    (EVENT_CATEGORY(evt) == EVT_CAT_BUTTON)
#define EVENT_IS_MOTOR(evt)     (EVENT_CATEGORY(evt) == EVT_CAT_MOTOR)
#define EVENT_IS_CONTROL(evt)   (EVENT_CATEGORY(evt) == EVT_CAT_CONTROL)
#define EVENT_IS_UI(evt)        (EVENT_CATEGORY(evt) == EVT_CAT_UI)

/* Create an event with type and optional payload */
static inline event_t event_create(uint8_t type, uint16_t payload, uint32_t timestamp) {
    event_t evt = {
        .type = type,
        .flags = 0,
        .payload16 = payload,
        .timestamp = timestamp
    };
    return evt;
}

/* Create a simple event with just type and timestamp */
static inline event_t event_simple(uint8_t type, uint32_t timestamp) {
    return event_create(type, 0, timestamp);
}

#endif /* KERNEL_EVENT_H */
