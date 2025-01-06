/*
 * Input Module
 *
 * Button tracking, quick actions, lock mode, and button mapping.
 */

#include "input.h"
#include "app_data.h"
#include "bus/bus.h"
#include "config/config.h"
#include "control/control.h"
#include "ui_state.h"
#include "src/profiles/profiles.h"

/* External globals from main */
extern volatile uint32_t g_ms;

/* Config access */

/* Global state instances */
button_track_t g_button_track;
uint8_t g_button_short_press;
uint8_t g_button_long_press;
uint8_t g_button_virtual;
uint8_t g_button_virtual_prev;
uint8_t g_lock_active;
uint8_t g_lock_allowed_mask;
uint8_t g_quick_action_last;

/* ================================================================
 * Button Tracking
 * ================================================================ */

void button_track_reset(void)
{
    button_track_reset_state(&g_button_track);
    g_button_short_press = 0;
    g_button_long_press = 0;
    g_button_virtual_prev = g_button_virtual;
}

void button_track_update(uint8_t buttons, uint8_t allowed_mask, uint8_t suppress_events)
{
    button_track_update_state(&g_button_track, buttons, allowed_mask, g_ms, suppress_events,
                              &g_button_short_press, &g_button_long_press);
}

/* ================================================================
 * Lock Mode
 * ================================================================ */

uint8_t lock_allowed_mask(uint8_t flags)
{
    uint8_t mask = 0;
    if (flags & BUTTON_FLAG_LOCK_ALLOW_PAGE)
        mask |= UI_PAGE_BUTTON_RAW | UI_PAGE_BUTTON_POWER;
    if (flags & BUTTON_FLAG_LOCK_ALLOW_GEAR)
        mask |= BUTTON_GEAR_UP_MASK | BUTTON_GEAR_DOWN_MASK;
    if (flags & BUTTON_FLAG_LOCK_ALLOW_CRUISE)
        mask |= CRUISE_BUTTON_MASK;
    if (flags & BUTTON_FLAG_LOCK_ALLOW_WALK)
        mask |= WALK_BUTTON_MASK;
    if (flags & BUTTON_FLAG_LOCK_ALLOW_PROFILE)
        mask |= 0x03u;
    return mask;
}

uint8_t lock_should_apply(uint8_t flags)
{
    if (!(flags & BUTTON_FLAG_LOCK_ENABLE))
        return 0;
    if (g_inputs.speed_dmph < LOCK_SPEED_MIN_DMPH)
        return 0;
    return 1;
}

/* ================================================================
 * Quick Actions
 * ================================================================ */

void quick_action_apply(quick_action_t action)
{
    if (action == QUICK_ACTION_NONE)
        return;
    g_quick_action_last = (uint8_t)action;
    switch (action)
    {
    case QUICK_ACTION_TOGGLE_CRUISE:
        g_cruise_toggle_request = 1u;
        break;
    case QUICK_ACTION_CYCLE_PROFILE:
    {
        uint8_t next = (uint8_t)(g_active_profile_id + 1u);
        if (next >= PROFILE_COUNT)
            next = 0u;
        set_active_profile(next, 1);
        break;
    }
    case QUICK_ACTION_TOGGLE_CAPTURE:
    {
        uint8_t enable = bus_capture_get_enabled() ? 0u : 1u;
        bus_capture_set_enabled(enable, enable);
        break;
    }
    case QUICK_ACTION_TOGGLE_FOCUS:
        if (g_ui_page == UI_PAGE_FOCUS)
        {
            g_ui_page = g_ui_focus_prev_page;
        }
        else
        {
            g_ui_focus_prev_page = g_ui_page;
            g_ui_page = UI_PAGE_FOCUS;
        }
        break;
    default:
        break;
    }
}

void quick_action_handle(uint8_t long_press_mask)
{
    if (long_press_mask & CRUISE_BUTTON_MASK)
    {
        if (g_config_active.flags & CFG_FLAG_QA_FOCUS)
            quick_action_apply(QUICK_ACTION_TOGGLE_FOCUS);
        else if (g_config_active.flags & CFG_FLAG_QA_CRUISE)
            quick_action_apply(QUICK_ACTION_TOGGLE_CRUISE);
    }
    if ((long_press_mask & BUTTON_GEAR_UP_MASK) && (g_config_active.flags & CFG_FLAG_QA_PROFILE))
        quick_action_apply(QUICK_ACTION_CYCLE_PROFILE);
    if ((long_press_mask & BUTTON_GEAR_DOWN_MASK) && (g_config_active.flags & CFG_FLAG_QA_CAPTURE))
        quick_action_apply(QUICK_ACTION_TOGGLE_CAPTURE);
}
