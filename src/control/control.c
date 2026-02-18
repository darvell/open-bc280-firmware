/*
 * Control Module
 *
 * Rider control features: cruise control, walk assist, regen braking,
 * drive modes, boost management, and soft start.
 */

#include "control.h"
#include "app_data.h"
#include "power/power.h"
#include "core/math_util.h"
#include "storage/logs.h"
#include "config/config.h"
#include "input/input.h"
#include "motor/motor_cmd.h"

/* Global config instance (defined in main.c) */
/* Global state instances */
cruise_state_t g_cruise;
regen_state_t g_regen;
drive_state_t g_drive;
boost_state_t g_boost;
soft_start_state_t g_soft_start;
walk_state_t g_walk_state;
uint8_t g_walk_inhibit;
uint16_t g_walk_cmd_power_w;
uint16_t g_walk_cmd_current_dA;
uint32_t g_walk_entry_ms;
uint8_t g_cruise_toggle_request;

/* ================================================================
 * Walk Assist
 * ================================================================ */

void walk_reset(void)
{
    g_walk_state = WALK_STATE_OFF;
    g_walk_inhibit = 0;
    g_walk_cmd_power_w = 0;
    g_walk_cmd_current_dA = 0;
    g_walk_entry_ms = 0;
}

int walk_capable(void)
{
    if (!(g_hw_caps & CAP_FLAG_WALK))
        return 0;
    if (!(g_config_active.flags & CAP_FLAG_WALK))
        return 0;
    return 1;
}

void walk_update(void)
{
    if (!walk_capable())
    {
        g_walk_state = WALK_STATE_DISABLED;
        g_walk_cmd_power_w = 0;
        g_walk_cmd_current_dA = 0;
        return;
    }

    uint8_t walk_btn = (g_inputs.buttons & WALK_BUTTON_MASK) ? 1u : 0u;

    /* Clear inhibit once button released and brake is clear */
    if (!walk_btn && g_inputs.brake == 0)
        g_walk_inhibit = 0;

    /* Brake always cancels and zeroes output */
    if (g_inputs.brake)
    {
        if (g_walk_state == WALK_STATE_ACTIVE)
            g_walk_state = WALK_STATE_CANCELLED;
        g_walk_inhibit = 1;
        g_walk_cmd_power_w = 0;
        g_walk_cmd_current_dA = 0;
        return;
    }

    /* Optional timeout auto-cancel */
    if (g_walk_state == WALK_STATE_ACTIVE && (WALK_TIMEOUT_MS > 0) &&
        (g_ms - g_walk_entry_ms) >= WALK_TIMEOUT_MS)
    {
        g_walk_state = WALK_STATE_CANCELLED;
        g_walk_inhibit = 1;
    }

    /* Entry */
    if (walk_btn && !g_walk_inhibit && g_walk_state != WALK_STATE_ACTIVE)
    {
        g_walk_state = WALK_STATE_ACTIVE;
        g_walk_entry_ms = g_ms;
    }

    if (g_walk_state != WALK_STATE_ACTIVE)
    {
        g_walk_cmd_power_w = 0;
        g_walk_cmd_current_dA = 0;
        return;
    }

    /* Active: taper command as speed approaches cap */
    uint16_t spd = g_inputs.speed_dmph;
    if (spd >= WALK_SPEED_CAP_DMPH)
    {
        g_walk_state = WALK_STATE_CANCELLED;
        g_walk_inhibit = 1;
        g_walk_cmd_power_w = 0;
        g_walk_cmd_current_dA = 0;
        return;
    }
    uint32_t span = WALK_SPEED_CAP_DMPH;
    uint32_t rem = span - spd;
    uint32_t cmd = (uint32_t)WALK_BASE_POWER_W * rem;
    cmd = (cmd + (span / 2u)) / span;
    if (cmd > 0xFFFF)
        cmd = 0xFFFF;
    g_walk_cmd_power_w = (uint16_t)cmd;
    g_walk_cmd_current_dA = (uint16_t)(g_walk_cmd_power_w / 2u);
}

/* ================================================================
 * Regen Braking
 * ================================================================ */

static uint8_t regen_clamp_level(uint8_t level)
{
    return (level > REGEN_LEVEL_MAX) ? REGEN_LEVEL_MAX : level;
}

uint8_t regen_capable(void)
{
    if (!(g_hw_caps & CAP_FLAG_REGEN))
        return 0;
    if (!(g_config_active.flags & CAP_FLAG_REGEN))
        return 0;
    return 1;
}

void regen_reset(void)
{
    g_regen.level = 0;
    g_regen.brake_level = 0;
    g_regen.active = 0;
    g_regen.cmd_power_w = 0;
    g_regen.cmd_current_dA = 0;
}

void regen_set_levels(uint8_t level, uint8_t brake_level)
{
    g_regen.level = regen_clamp_level(level);
    g_regen.brake_level = regen_clamp_level(brake_level);
}

void regen_update(void)
{
    if (!regen_capable())
    {
        regen_reset();
        return;
    }

    uint8_t target = g_regen.level;
    if (g_inputs.brake && g_regen.brake_level > target)
        target = g_regen.brake_level;

    if (target == 0)
    {
        g_regen.active = 0;
        g_regen.cmd_power_w = 0;
        g_regen.cmd_current_dA = 0;
        return;
    }

    uint32_t base = (uint32_t)target * (uint32_t)REGEN_STEP_W;
    if (base > 0xFFFFu)
        base = 0xFFFFu;
    uint16_t thermal_factor = g_power_policy.thermal_factor_q16 ? g_power_policy.thermal_factor_q16 : Q16_ONE;
    uint16_t limited = apply_q16((uint16_t)base, thermal_factor);
    g_regen.cmd_power_w = limited;
    g_regen.cmd_current_dA = (uint16_t)(limited / 2u);
    g_regen.active = (g_regen.cmd_power_w > 0) ? 1u : 0u;
}

/* ================================================================
 * Cruise Control
 * ================================================================ */

void cruise_reset(void)
{
    g_cruise.mode = CRUISE_OFF;
    g_cruise.last_button = 0;
    g_cruise.require_pedaling = 0;
    g_cruise.resume_available = 0;
    g_cruise.resume_require_pedaling = 0;
    g_cruise.resume_block_reason = CRUISE_RESUME_NONE;
    g_cruise.resume_mode = CRUISE_OFF;
    g_cruise.set_speed_dmph = 0;
    g_cruise.set_power_w = 0;
    g_cruise.output_w = 0;
}

void cruise_cancel(uint8_t reason)
{
    if (g_cruise.mode == CRUISE_OFF)
        return;
    g_cruise.resume_available = 1;
    g_cruise.resume_require_pedaling = g_cruise.require_pedaling;
    g_cruise.resume_mode = (uint8_t)g_cruise.mode;
    g_cruise.resume_block_reason = CRUISE_RESUME_NONE;
    g_cruise.mode = CRUISE_OFF;
    g_cruise.output_w = 0;
    g_cruise.require_pedaling = 0;
    event_log_append(EVT_CRUISE_EVENT, reason);
}

static cruise_resume_reason_t cruise_resume_block_reason(void)
{
    if (g_inputs.brake)
        return CRUISE_RESUME_BLOCK_BRAKE;
    if (motor_cmd_link_fault_active() || g_motor.err)
        return CRUISE_RESUME_BLOCK_FAULT;

    uint16_t speed = g_inputs.speed_dmph;
    uint16_t set_speed = g_cruise.set_speed_dmph;
    uint16_t delta = (speed > set_speed) ? (speed - set_speed) : (set_speed - speed);
    if (delta > CRUISE_RESUME_SPEED_DELTA_DMPH)
        return CRUISE_RESUME_BLOCK_SPEED;

    if (g_cruise.resume_require_pedaling && g_inputs.cadence_rpm == 0)
        return CRUISE_RESUME_BLOCK_PEDAL;

    if (g_power_policy.limit_reason != LIMIT_REASON_USER)
        return CRUISE_RESUME_BLOCK_LIMIT;

    return CRUISE_RESUME_NONE;
}

uint16_t cruise_apply(uint16_t base_power, uint16_t limit_power)
{
    uint8_t press = (g_button_short_press & CRUISE_BUTTON_MASK) ? 1u : 0u;
    if (g_cruise_toggle_request)
    {
        press = 1u;
        g_cruise_toggle_request = 0;
    }
    g_cruise.last_button = (g_inputs.buttons & CRUISE_BUTTON_MASK) ? 1u : 0u;

    if (g_inputs.brake)
    {
        if (g_cruise.mode != CRUISE_OFF)
        {
            cruise_cancel(CRUISE_EVT_CANCEL_BRAKE);
        }
        else if (press && g_cruise.resume_available)
        {
            g_cruise.resume_block_reason = CRUISE_RESUME_BLOCK_BRAKE;
            event_log_append(EVT_CRUISE_EVENT, CRUISE_EVT_RESUME_BLOCK_BRAKE);
        }
        return base_power;
    }

    if (g_walk_state == WALK_STATE_ACTIVE)
    {
        cruise_cancel(CRUISE_EVT_CANCEL_WALK);
        return base_power;
    }

    if (g_cruise.mode != CRUISE_OFF && g_cruise.require_pedaling && g_inputs.cadence_rpm == 0)
    {
        cruise_cancel(CRUISE_EVT_CANCEL_PEDAL);
        return base_power;
    }

    if (press)
    {
        if (g_cruise.mode != CRUISE_OFF)
        {
            cruise_cancel(CRUISE_EVT_CANCEL_USER);
            return base_power;
        }

        if (g_cruise.resume_available)
        {
            cruise_resume_reason_t reason = cruise_resume_block_reason();
            if (reason == CRUISE_RESUME_NONE &&
                (g_cruise.resume_mode == CRUISE_SPEED || g_cruise.resume_mode == CRUISE_POWER))
            {
                g_cruise.mode = (cruise_mode_t)g_cruise.resume_mode;
                g_cruise.require_pedaling = g_cruise.resume_require_pedaling;
                if (g_cruise.mode == CRUISE_SPEED &&
                    g_effective_cap_speed_dmph &&
                    g_cruise.set_speed_dmph > g_effective_cap_speed_dmph)
                {
                    g_cruise.set_speed_dmph = g_effective_cap_speed_dmph;
                }
                g_cruise.output_w = g_cruise.set_power_w;
                if (g_cruise.output_w == 0)
                    g_cruise.output_w = base_power;
                g_cruise.resume_available = 0;
                g_cruise.resume_block_reason = CRUISE_RESUME_NONE;
                event_log_append(EVT_CRUISE_EVENT, CRUISE_EVT_RESUME_OK);
            }
            else
            {
                uint8_t flag = CRUISE_EVT_RESUME_BLOCK_LIMIT;
                if (reason == CRUISE_RESUME_NONE)
                    reason = CRUISE_RESUME_BLOCK_LIMIT;
                switch (reason)
                {
                    case CRUISE_RESUME_BLOCK_BRAKE:
                        flag = CRUISE_EVT_RESUME_BLOCK_BRAKE;
                        break;
                    case CRUISE_RESUME_BLOCK_SPEED:
                        flag = CRUISE_EVT_RESUME_BLOCK_SPEED;
                        break;
                    case CRUISE_RESUME_BLOCK_PEDAL:
                        flag = CRUISE_EVT_RESUME_BLOCK_PEDAL;
                        break;
                    case CRUISE_RESUME_BLOCK_FAULT:
                        flag = CRUISE_EVT_RESUME_BLOCK_FAULT;
                        break;
                    case CRUISE_RESUME_BLOCK_LIMIT:
                    default:
                        flag = CRUISE_EVT_RESUME_BLOCK_LIMIT;
                        break;
                }
                g_cruise.resume_block_reason = (uint8_t)reason;
                event_log_append(EVT_CRUISE_EVENT, flag);
                return base_power;
            }
        }
        else
        {
            if (g_inputs.speed_dmph < CRUISE_MIN_SPEED_DMPH)
                return base_power;

            cruise_mode_t mode = (g_inputs.buttons & CRUISE_SPEED_SELECT_MASK) ? CRUISE_SPEED : CRUISE_POWER;
            if (mode == CRUISE_POWER && base_power < CRUISE_MIN_POWER_W)
                mode = CRUISE_SPEED;

            g_cruise.mode = mode;
            g_cruise.set_speed_dmph = g_inputs.speed_dmph;
            if (g_effective_cap_speed_dmph && g_cruise.set_speed_dmph > g_effective_cap_speed_dmph)
                g_cruise.set_speed_dmph = g_effective_cap_speed_dmph;
            g_cruise.set_power_w = base_power;
            if (g_cruise.set_power_w == 0)
                g_cruise.set_power_w = g_outputs.cmd_power_w;
            g_cruise.output_w = g_cruise.set_power_w;
            g_cruise.require_pedaling = (g_inputs.cadence_rpm > 0);
            g_cruise.resume_available = 0;
            g_cruise.resume_block_reason = CRUISE_RESUME_NONE;

            event_log_append(EVT_CRUISE_EVENT,
                             (mode == CRUISE_SPEED) ? CRUISE_EVT_ENGAGE_SPEED : CRUISE_EVT_ENGAGE_POWER);
        }
    }

    if (g_cruise.mode == CRUISE_SPEED)
    {
        int16_t err = (int16_t)g_cruise.set_speed_dmph - (int16_t)g_inputs.speed_dmph;
        int32_t delta = (int32_t)err * (int32_t)CRUISE_SPEED_KP_W_PER_DMPH;
        if (delta > (int32_t)CRUISE_SPEED_STEP_MAX_W)
            delta = CRUISE_SPEED_STEP_MAX_W;
        else if (delta < -(int32_t)CRUISE_SPEED_STEP_MAX_W)
            delta = -(int32_t)CRUISE_SPEED_STEP_MAX_W;
        int32_t target = (int32_t)g_cruise.output_w + delta;
        if (target < 0)
            target = 0;
        if (limit_power && target > (int32_t)limit_power)
            target = limit_power;
        g_cruise.output_w = (uint16_t)target;
        return g_cruise.output_w;
    }

    if (g_cruise.mode == CRUISE_POWER)
    {
        uint16_t target = g_cruise.set_power_w;
        if (limit_power && target > limit_power)
            target = limit_power;
        g_cruise.output_w = target;
        return g_cruise.output_w;
    }

    return base_power;
}

/* ================================================================
 * Drive Modes and Manual Control
 * ================================================================ */

void drive_reset(void)
{
    g_drive.mode = DRIVE_MODE_AUTO;
    g_drive.setpoint = 0;
    g_drive.cmd_power_w = 0;
    g_drive.cmd_current_dA = 0;
    g_drive.last_ms = 0;
}

void drive_apply_config(void)
{
    uint16_t setpoint = 0;
    if (g_config_active.drive_mode == DRIVE_MODE_MANUAL_CURRENT)
        setpoint = g_config_active.manual_current_dA;
    else if (g_config_active.drive_mode == DRIVE_MODE_MANUAL_POWER)
        setpoint = g_config_active.manual_power_w;
    g_drive.mode = (drive_mode_t)g_config_active.drive_mode;
    g_drive.setpoint = setpoint;
    g_drive.cmd_power_w = 0;
    g_drive.cmd_current_dA = 0;
    g_drive.last_ms = 0;
    boost_reset();
}

uint16_t battery_power_w(void)
{
    if ((g_input_caps & INPUT_CAP_BATT_V) && (g_input_caps & INPUT_CAP_BATT_I))
    {
        int32_t v = g_inputs.battery_dV;
        int32_t a = g_inputs.battery_dA;
        int32_t p = (v * a) / 100;
        if (p < 0)
            p = 0;
        if (p > 0xFFFF)
            p = 0xFFFF;
        return (uint16_t)p;
    }
    return g_inputs.power_w;
}

uint16_t manual_power_apply(uint16_t target_w)
{
    uint32_t now = g_ms;
    uint32_t dt = (g_drive.last_ms == 0) ? 0u : (now - g_drive.last_ms);
    g_drive.last_ms = now;
    if (g_drive.cmd_power_w == 0 || dt == 0)
        g_drive.cmd_power_w = target_w;

    uint16_t measured = battery_power_w();
    int32_t err = (int32_t)target_w - (int32_t)measured;
    int32_t delta = (err * (int32_t)MANUAL_POWER_KP_Q15) >> 15;
    uint32_t step = (MANUAL_POWER_RATE_WPS * dt + 500u) / 1000u;
    if (step == 0)
        step = 1u;
    if (delta > (int32_t)step)
        delta = (int32_t)step;
    else if (delta < -(int32_t)step)
        delta = -(int32_t)step;
    int32_t next = (int32_t)g_drive.cmd_power_w + delta;
    if (next < 0)
        next = 0;
    if (next > (int32_t)MANUAL_POWER_MAX_W)
        next = (int32_t)MANUAL_POWER_MAX_W;
    g_drive.cmd_power_w = (uint16_t)next;
    return g_drive.cmd_power_w;
}

/* ================================================================
 * Boost Management
 * ================================================================ */

void boost_reset(void)
{
    g_boost.budget_ms = g_config_active.boost_budget_ms;
    g_boost.active = 0;
    g_boost.last_ms = 0;
}

void boost_update(void)
{
    uint32_t now = g_ms;
    uint32_t dt = (g_boost.last_ms == 0) ? 0u : (now - g_boost.last_ms);
    g_boost.last_ms = now;
    g_boost.active = 0;
    if (g_drive.mode != DRIVE_MODE_SPORT || g_config_active.boost_budget_ms == 0)
    {
        g_boost.budget_ms = g_config_active.boost_budget_ms;
        return;
    }

    uint16_t budget = g_boost.budget_ms;
    if (dt == 0)
        return;
    uint16_t threshold = g_config_active.boost_threshold_dA;
    uint16_t i_phase = (uint16_t)g_power_policy.i_phase_est_dA;
    if (i_phase > threshold)
    {
        uint32_t excess = (uint32_t)(i_phase - threshold);
        uint32_t burn = ((uint32_t)excess * (uint32_t)g_config_active.boost_gain_q15 + (1u << 14)) >> 15;
        if (burn == 0)
            burn = 1u;
        uint32_t dec = burn * dt;
        if (dec >= budget)
            budget = 0;
        else
            budget = (uint16_t)(budget - dec);
        g_boost.active = (budget > 0) ? 1u : 0u;
    }
    else
    {
        uint32_t cooldown = g_config_active.boost_cooldown_ms;
        if (cooldown == 0)
            cooldown = 1u;
        uint32_t inc = ((uint32_t)g_config_active.boost_budget_ms * dt + (cooldown / 2u)) / cooldown;
        uint32_t next = (uint32_t)budget + inc;
        if (next > g_config_active.boost_budget_ms)
            next = g_config_active.boost_budget_ms;
        budget = (uint16_t)next;
    }
    g_boost.budget_ms = budget;
}

/* ================================================================
 * Soft Start
 * ================================================================ */

void soft_start_reset(void)
{
    g_soft_start.active = 0;
    g_soft_start.target_w = 0;
    g_soft_start.output_w = 0;
    g_soft_start.last_ms = 0;
}

uint16_t soft_start_apply(uint16_t desired_w)
{
    if (g_inputs.brake)
    {
        soft_start_reset();
        return 0;
    }

    uint16_t rate = g_config_active.soft_start_ramp_wps;
    if (rate == 0)
    {
        g_soft_start.active = 0;
        g_soft_start.target_w = desired_w;
        g_soft_start.output_w = desired_w;
        g_soft_start.last_ms = g_ms;
        return desired_w;
    }

    uint16_t deadband = g_config_active.soft_start_deadband_w;
    if (deadband > SOFT_START_DEADBAND_MAX_W)
        deadband = SOFT_START_DEADBAND_MAX_W;
    if (desired_w <= deadband)
    {
        soft_start_reset();
        return 0;
    }

    uint32_t now = g_ms;
    uint32_t dt = (g_soft_start.last_ms == 0) ? 0u : (now - g_soft_start.last_ms);
    g_soft_start.last_ms = now;
    g_soft_start.target_w = desired_w;

    if (!g_soft_start.active)
    {
        g_soft_start.active = 1;
        uint16_t kick = g_config_active.soft_start_kick_w;
        if (kick > SOFT_START_KICK_MAX_W)
            kick = SOFT_START_KICK_MAX_W;
        if (kick && desired_w > kick)
            desired_w = kick;
        g_soft_start.output_w = desired_w;
        return desired_w;
    }

    if (desired_w <= g_soft_start.output_w)
    {
        g_soft_start.output_w = desired_w;
        return desired_w;
    }

    uint32_t rate_u32 = rate;
    if (rate_u32 < SOFT_START_RAMP_MIN_WPS)
        rate_u32 = SOFT_START_RAMP_MIN_WPS;
    if (rate_u32 > SOFT_START_RAMP_MAX_WPS)
        rate_u32 = SOFT_START_RAMP_MAX_WPS;
    uint32_t step = (dt * rate_u32) / 1000u;
    if (step == 0u && dt > 0u)
        step = 1u;
    uint32_t next = (uint32_t)g_soft_start.output_w + step;
    if (next > desired_w)
        next = desired_w;
    if (next > 0xFFFFu)
        next = 0xFFFFu;
    g_soft_start.output_w = (uint16_t)next;
    return g_soft_start.output_w;
}
