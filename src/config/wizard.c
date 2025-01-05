#include "config.h"

#include "app_data.h"
#include "control/control.h"
#include "input/input.h"
#include "src/profiles/profiles.h"
#include "storage/logs.h"

#define WIZARD_BUTTON_UP    BUTTON_GEAR_UP_MASK
#define WIZARD_BUTTON_DOWN  BUTTON_GEAR_DOWN_MASK
#define WIZARD_BUTTON_BACK  WALK_BUTTON_MASK
#define WIZARD_BUTTON_NEXT  UI_PAGE_BUTTON_RAW
#define WIZARD_BUTTON_START (WIZARD_BUTTON_BACK | WIZARD_BUTTON_NEXT)
#define WIZARD_WHEEL_STEP_MM 50u
#define WIZARD_WHEEL_MIN_MM 100u
#define WIZARD_WHEEL_MAX_MM 6000u
#define CONFIG_CHANGE_MAX_SPEED_DMPH 10u /* 1.0 mph */

static wizard_state_t g_wizard;

static uint16_t config_change_speed_dmph(void)
{
    uint16_t spd = g_inputs.speed_dmph;
    if (g_motor.speed_dmph > spd)
        spd = g_motor.speed_dmph;
    return spd;
}

void wizard_reset(void)
{
    g_wizard.active = 0;
    g_wizard.step = WIZ_STEP_WHEEL;
    g_wizard.error = 0;
    g_wizard.last_buttons = 0;
}

void wizard_start(void)
{
    g_wizard.active = 1;
    g_wizard.step = WIZ_STEP_WHEEL;
    g_wizard.error = 0;
    g_wizard.last_buttons = 0;
    g_wizard.cfg = g_config_active;
}

static void wizard_adjust(int dir)
{
    if (dir == 0)
        return;
    g_wizard.error = 0;
    switch (g_wizard.step)
    {
    case WIZ_STEP_WHEEL:
    {
        int32_t v = (int32_t)g_wizard.cfg.wheel_mm + (int32_t)(dir * (int)WIZARD_WHEEL_STEP_MM);
        if (v < (int32_t)WIZARD_WHEEL_MIN_MM)
            v = WIZARD_WHEEL_MIN_MM;
        if (v > (int32_t)WIZARD_WHEEL_MAX_MM)
            v = WIZARD_WHEEL_MAX_MM;
        g_wizard.cfg.wheel_mm = (uint16_t)v;
    }
    break;
    case WIZ_STEP_UNITS:
        g_wizard.cfg.units = g_wizard.cfg.units ? 0u : 1u;
        break;
    case WIZ_STEP_BUTTONS:
    {
        int32_t v = (int32_t)g_wizard.cfg.button_map + dir;
        if (v < 0)
            v = (int32_t)BUTTON_MAP_MAX;
        if (v > (int32_t)BUTTON_MAP_MAX)
            v = 0;
        g_wizard.cfg.button_map = (uint8_t)v;
    }
    break;
    case WIZ_STEP_PROFILE:
    {
        int32_t v = (int32_t)g_wizard.cfg.profile_id + dir;
        if (v < 0)
            v = (int32_t)(PROFILE_COUNT - 1);
        if (v >= (int32_t)PROFILE_COUNT)
            v = 0;
        g_wizard.cfg.profile_id = (uint8_t)v;
    }
    break;
    case WIZ_STEP_DONE:
    default:
        break;
    }
}

static uint8_t wizard_commit(config_reject_reason_t *reason_out)
{
    config_t tmp = g_wizard.cfg;
    tmp.version = CONFIG_VERSION;
    tmp.size = CONFIG_BLOB_SIZE;
    tmp.seq = g_config_active.seq + 1u;
    tmp.crc32 = 0;
    tmp.crc32 = config_crc_expected(&tmp);

    config_reject_reason_t reason = CFG_REJECT_NONE;
    if (config_change_speed_dmph() > CONFIG_CHANGE_MAX_SPEED_DMPH)
    {
        reason = CFG_REJECT_POLICY;
        goto fail;
    }
    if (!config_validate_reason(&tmp, 1, &reason))
        goto fail;
    if (!config_policy_validate(&tmp, &reason))
        goto fail;

    config_commit_active(&tmp);
    if (reason_out)
        *reason_out = CFG_REJECT_NONE;
    return 1;

fail:
    if (reason_out)
        *reason_out = reason;
    return 0;
}

void wizard_handle_buttons(uint8_t buttons)
{
    uint8_t rising = (uint8_t)(buttons & (uint8_t)~g_wizard.last_buttons);
    uint8_t changed = 0;

    if (!g_wizard.active)
    {
        if ((buttons & WIZARD_BUTTON_START) == WIZARD_BUTTON_START && (rising & WIZARD_BUTTON_START))
        {
            wizard_start();
            changed = 1u;
        }
        g_wizard.last_buttons = buttons;
        (void)changed;
        return;
    }

    if (rising & WIZARD_BUTTON_BACK)
    {
        if (g_wizard.step > WIZ_STEP_WHEEL)
            g_wizard.step = (wizard_step_t)(g_wizard.step - 1);
        else
            wizard_reset();
        changed = 1u;
    }

    if (rising & WIZARD_BUTTON_NEXT)
    {
        if (g_wizard.step < WIZ_STEP_DONE)
        {
            g_wizard.step = (wizard_step_t)(g_wizard.step + 1);
            g_wizard.error = 0;
        }
        else
        {
            config_reject_reason_t reason = CFG_REJECT_NONE;
            if (wizard_commit(&reason))
                wizard_reset();
            else
                g_wizard.error = (uint8_t)reason;
        }
        changed = 1u;
    }

    if (rising & WIZARD_BUTTON_UP)
    {
        wizard_adjust(1);
        changed = 1u;
    }
    if (rising & WIZARD_BUTTON_DOWN)
    {
        wizard_adjust(-1);
        changed = 1u;
    }

    g_wizard.last_buttons = buttons;
    (void)changed;
}

void wizard_get_state(wizard_state_t *out)
{
    if (!out)
        return;
    *out = g_wizard;
}

uint8_t wizard_is_active(void)
{
    return g_wizard.active ? 1u : 0u;
}
