#ifndef INPUT_H
#define INPUT_H

#include <stdint.h>

#include "button_fsm.h"
#include "control/control.h"
#define UI_PAGE_BUTTON_RAW   0x04u
#define UI_PAGE_BUTTON_POWER 0x08u

/* Button flag constants */
#define BUTTON_FLAG_LOCK_ENABLE      (1u << 0)
#define BUTTON_FLAG_LOCK_ALLOW_PAGE  (1u << 1)
#define BUTTON_FLAG_LOCK_ALLOW_GEAR  (1u << 2)
#define BUTTON_FLAG_LOCK_ALLOW_CRUISE (1u << 3)
#define BUTTON_FLAG_LOCK_ALLOW_WALK  (1u << 4)
#define BUTTON_FLAG_LOCK_ALLOW_PROFILE (1u << 5)
#define BUTTON_FLAG_FOCUS_POWER       (1u << 6)

/* Button mapping presets */
#define BUTTON_MAP_MAX 2u

/* Allowed config flags for button behavior */
#define BUTTON_FLAGS_ALLOWED (BUTTON_FLAG_LOCK_ENABLE | BUTTON_FLAG_LOCK_ALLOW_PAGE | \
                              BUTTON_FLAG_LOCK_ALLOW_GEAR | BUTTON_FLAG_LOCK_ALLOW_CRUISE | \
                              BUTTON_FLAG_LOCK_ALLOW_WALK | BUTTON_FLAG_LOCK_ALLOW_PROFILE | \
                              BUTTON_FLAG_FOCUS_POWER)

/* Lock speed threshold */
#define LOCK_SPEED_MIN_DMPH  5u

/* Button state tracking */
typedef struct {
    button_fsm_t fsm;
    uint8_t extra_last;
    uint8_t extra_long_fired;
    uint32_t extra_press_start_ms;
} button_track_t;

static inline void button_track_reset_state(button_track_t *track)
{
    if (!track)
        return;
    button_fsm_init(&track->fsm);
    track->extra_last = 0;
    track->extra_long_fired = 0;
    track->extra_press_start_ms = 0;
}

static inline void button_track_update_state(button_track_t *track, uint8_t buttons,
                                             uint8_t allowed_mask, uint32_t now_ms,
                                             uint8_t suppress_events,
                                             uint8_t *short_press_out, uint8_t *long_press_out)
{
    uint8_t short_press = 0;
    uint8_t long_press = 0;
    if (!track)
    {
        if (short_press_out)
            *short_press_out = 0;
        if (long_press_out)
            *long_press_out = 0;
        return;
    }

    uint8_t filtered = (uint8_t)(buttons & allowed_mask);
    uint8_t fsm_buttons = 0;
    if (filtered & BUTTON_GEAR_UP_MASK)
        fsm_buttons |= BTN_MASK_UP;
    if (filtered & BUTTON_GEAR_DOWN_MASK)
        fsm_buttons |= BTN_MASK_DOWN;
    if (filtered & UI_PAGE_BUTTON_RAW)
        fsm_buttons |= BTN_MASK_MENU;
    if (filtered & UI_PAGE_BUTTON_POWER)
        fsm_buttons |= BTN_MASK_POWER;

    button_fsm_update(&track->fsm, fsm_buttons, now_ms);

    event_t evt;
    while (button_fsm_poll_event(&track->fsm, &evt))
    {
        uint32_t duration = evt.timestamp - track->fsm.press_start_ms;
        uint8_t is_long = (uint8_t)(duration >= BTN_LONG_THRESHOLD_MS);
        if (suppress_events)
            continue;
        switch (evt.type)
        {
        case EVT_BTN_SHORT_UP:
            short_press |= BUTTON_GEAR_UP_MASK;
            break;
        case EVT_BTN_SHORT_DOWN:
            short_press |= BUTTON_GEAR_DOWN_MASK;
            break;
        case EVT_BTN_SHORT_MENU:
            short_press |= UI_PAGE_BUTTON_RAW;
            break;
        case EVT_BTN_SHORT_POWER:
            short_press |= UI_PAGE_BUTTON_POWER;
            break;
        case EVT_BTN_LONG_UP:
            long_press |= BUTTON_GEAR_UP_MASK;
            break;
        case EVT_BTN_LONG_DOWN:
            long_press |= BUTTON_GEAR_DOWN_MASK;
            break;
        case EVT_BTN_LONG_MENU:
            long_press |= UI_PAGE_BUTTON_RAW;
            break;
        case EVT_BTN_LONG_POWER:
            long_press |= UI_PAGE_BUTTON_POWER;
            break;
        case EVT_BTN_COMBO_UP_DOWN:
            if (is_long)
                long_press |= (BUTTON_GEAR_UP_MASK | BUTTON_GEAR_DOWN_MASK);
            else
                short_press |= (BUTTON_GEAR_UP_MASK | BUTTON_GEAR_DOWN_MASK);
            if (allowed_mask & WALK_BUTTON_MASK)
            {
                if (is_long)
                    long_press |= WALK_BUTTON_MASK;
                else
                    short_press |= WALK_BUTTON_MASK;
            }
            break;
        case EVT_BTN_COMBO_UP_MENU:
            if (is_long)
                long_press |= (BUTTON_GEAR_UP_MASK | UI_PAGE_BUTTON_RAW);
            else
                short_press |= (BUTTON_GEAR_UP_MASK | UI_PAGE_BUTTON_RAW);
            break;
        case EVT_BTN_COMBO_DOWN_MENU:
            if (is_long)
                long_press |= (BUTTON_GEAR_DOWN_MASK | UI_PAGE_BUTTON_RAW);
            else
                short_press |= (BUTTON_GEAR_DOWN_MASK | UI_PAGE_BUTTON_RAW);
            break;
        case EVT_BTN_COMBO_MENU_POWER:
            if (is_long)
                long_press |= (UI_PAGE_BUTTON_RAW | UI_PAGE_BUTTON_POWER);
            else
                short_press |= (UI_PAGE_BUTTON_RAW | UI_PAGE_BUTTON_POWER);
            break;
        default:
            break;
        }
    }

    if (filtered & CRUISE_BUTTON_MASK)
    {
        if (!(track->extra_last & CRUISE_BUTTON_MASK))
        {
            track->extra_press_start_ms = now_ms;
            track->extra_long_fired = 0;
        }
        else if (!track->extra_long_fired)
        {
            if ((uint32_t)(now_ms - track->extra_press_start_ms) >= BTN_LONG_THRESHOLD_MS)
            {
                track->extra_long_fired = 1u;
                if (!suppress_events)
                    long_press |= CRUISE_BUTTON_MASK;
            }
        }
    }
    else if (track->extra_last & CRUISE_BUTTON_MASK)
    {
        if (!track->extra_long_fired)
        {
            if ((uint32_t)(now_ms - track->extra_press_start_ms) < BTN_LONG_THRESHOLD_MS)
            {
                if (!suppress_events)
                    short_press |= CRUISE_BUTTON_MASK;
            }
        }
        track->extra_long_fired = 0;
        track->extra_press_start_ms = 0;
    }

    track->extra_last = (uint8_t)(filtered & CRUISE_BUTTON_MASK);
    if (short_press_out)
        *short_press_out = short_press;
    if (long_press_out)
        *long_press_out = long_press;
}

/* Quick action types */
typedef enum {
    QUICK_ACTION_NONE = 0,
    QUICK_ACTION_TOGGLE_CRUISE = 1,
    QUICK_ACTION_CYCLE_PROFILE = 2,
    QUICK_ACTION_TOGGLE_CAPTURE = 3,
    QUICK_ACTION_TOGGLE_FOCUS = 4,
} quick_action_t;

/* Global state */
extern button_track_t g_button_track;
extern uint8_t g_button_short_press;
extern uint8_t g_button_long_press;
extern uint8_t g_button_virtual;
extern uint8_t g_button_virtual_prev;
extern uint8_t g_lock_active;
extern uint8_t g_lock_allowed_mask;
extern uint8_t g_quick_action_last;

/* API declarations */
void button_track_reset(void);
void button_track_update(uint8_t buttons, uint8_t allowed_mask, uint8_t suppress_events);
void buttons_tick(void);

void quick_action_apply(quick_action_t action);
void quick_action_handle(uint8_t long_press_mask);

static inline uint8_t button_map_apply(uint8_t buttons, uint8_t map)
{
    uint8_t out = buttons;
    if (map == 1u)
    {
        /* Swap UP and DOWN */
        uint8_t up = (uint8_t)(buttons & BUTTON_GEAR_UP_MASK);
        uint8_t down = (uint8_t)(buttons & BUTTON_GEAR_DOWN_MASK);
        out = (uint8_t)(buttons & (uint8_t)~(BUTTON_GEAR_UP_MASK | BUTTON_GEAR_DOWN_MASK));
        if (up)
            out |= BUTTON_GEAR_DOWN_MASK;
        if (down)
            out |= BUTTON_GEAR_UP_MASK;
    }
    else if (map == 2u)
    {
        /* Swap WALK and CRUISE */
        uint8_t walk = (uint8_t)(buttons & WALK_BUTTON_MASK);
        uint8_t cruise = (uint8_t)(buttons & CRUISE_BUTTON_MASK);
        out = (uint8_t)(buttons & (uint8_t)~(WALK_BUTTON_MASK | CRUISE_BUTTON_MASK));
        if (walk)
            out |= CRUISE_BUTTON_MASK;
        if (cruise)
            out |= WALK_BUTTON_MASK;
    }
    return out;
}
uint8_t lock_allowed_mask(uint8_t flags);
uint8_t lock_should_apply(uint8_t flags);

#endif /* INPUT_H */
