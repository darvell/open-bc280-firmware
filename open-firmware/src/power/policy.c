/*
 * Power Policy Governor
 *
 * Multi-governor power limiting system:
 * - Lug governor: reduces power at low duty cycle (motor stall prevention)
 * - Thermal governor: reduces power based on temperature/I²t model
 * - Sag governor: reduces power when battery voltage drops
 */

#include "power.h"
#include "app_data.h"
#include "core/math_util.h"
#include "storage/logs.h"

#define LIMIT_LOG_PERIOD_MS 2000u

/* Adaptive assist tuning */
#define ADAPT_EFFORT_SPEED_TAU_MS       2000u
#define ADAPT_EFFORT_POWER_TAU_MS       1500u
#define ADAPT_EFFORT_MIN_ERR_DMPH       8u    /* 0.8 mph */
#define ADAPT_EFFORT_GAIN_W_PER_DMPH    2u
#define ADAPT_EFFORT_MAX_BOOST_W        180u
#define ADAPT_EFFORT_MAX_BOOST_Q15      16384u /* 0.5x base */
#define ADAPT_EFFORT_TREND_SPEED_DMPH   15u
#define ADAPT_EFFORT_TREND_POWER_W      40u
#define ADAPT_EFFORT_TREND_GAIN_Q15     40960u /* 1.25x */
#define ADAPT_EFFORT_MIN_BASE_W         20u
#define ADAPT_EFFORT_MAX_ERR_DMPH       400u

#define ADAPT_ECO_RATE_UP_WPS           240u
#define ADAPT_ECO_RATE_SPIKE_WPS        120u
#define ADAPT_ECO_SPIKE_RATE_DMPH_S     60u

/* Global state instances */
power_policy_state_t g_power_policy;
adaptive_assist_state_t g_adapt;
static uint32_t g_adapt_dt_ms;
static uint16_t g_adapt_speed_dmph;

void power_policy_reset(void)
{
    g_power_policy.p_user_w = 0;
    g_power_policy.p_lug_w = 0;
    g_power_policy.p_thermal_w = 0;
    g_power_policy.p_sag_w = 0;
    g_power_policy.p_final_w = 0;
    g_power_policy.duty_q16 = 0;
    g_power_policy.i_phase_est_dA = 0;
    g_power_policy.thermal_state = 0;
    g_power_policy.thermal_factor_q16 = Q16_ONE;
    g_power_policy.sag_margin_dV = 0;
    g_power_policy.limit_reason = LIMIT_REASON_USER;
    g_power_policy.lug_factor_q16 = Q16_ONE;
    g_power_policy.thermal_fast = 0;
    g_power_policy.thermal_slow = 0;
    g_power_policy.last_ms = 0;
    g_power_policy.last_log_ms = 0;
    g_power_policy.last_reason = LIMIT_REASON_USER;
}

void power_policy_apply(uint16_t p_user_w)
{
    uint32_t now = g_ms;
    uint32_t dt = (g_power_policy.last_ms == 0) ? 0u : (now - g_power_policy.last_ms);
    g_power_policy.last_ms = now;

    g_power_policy.p_user_w = p_user_w;
    g_power_policy.p_lug_w = p_user_w;
    g_power_policy.p_thermal_w = p_user_w;
    g_power_policy.p_sag_w = p_user_w;
    g_power_policy.p_final_w = p_user_w;
    g_power_policy.limit_reason = LIMIT_REASON_USER;

    /* ---- Lug governor (duty-cycle based) ---- */
    uint16_t duty_q16 = 0;
    uint16_t lug_target = Q16_ONE;
    if (g_input_caps & INPUT_CAP_BATT_V)
    {
        int32_t v_batt = g_inputs.battery_dV;
        if (v_batt < 0)
            v_batt = 0;
        uint32_t v_nl = ((uint32_t)v_batt * (uint32_t)LUG_KV_Q16) >> 16;
        if (v_nl < LUG_VNL_MIN_DMPH)
            v_nl = LUG_VNL_MIN_DMPH;
        if (v_nl > 0)
        {
            uint32_t num = ((uint32_t)g_inputs.speed_dmph << 16);
            duty_q16 = clamp_u16(num / v_nl, 0, Q16_ONE);
        }
        if (duty_q16 < DUTY_MIN_Q16)
            duty_q16 = DUTY_MIN_Q16;

        if (duty_q16 >= LUG_D_START_Q16)
        {
            lug_target = Q16_ONE;
        }
        else if (duty_q16 <= LUG_D_HARD_Q16)
        {
            lug_target = LUG_F_MIN_Q16;
        }
        else
        {
            uint32_t span = (uint32_t)(LUG_D_START_Q16 - LUG_D_HARD_Q16);
            uint32_t num = (uint32_t)(LUG_D_START_Q16 - duty_q16) * (uint32_t)(Q16_ONE - LUG_F_MIN_Q16);
            lug_target = (uint16_t)(Q16_ONE - (num / span));
            if (lug_target < LUG_F_MIN_Q16)
                lug_target = LUG_F_MIN_Q16;
        }
    }

    g_power_policy.duty_q16 = duty_q16;

    if (p_user_w == 0)
        lug_target = Q16_ONE;

    /* Lug factor ramp */
    uint16_t lug_factor = g_power_policy.lug_factor_q16;
    if (lug_factor == 0)
        lug_factor = Q16_ONE;
    uint32_t span = (uint32_t)(Q16_ONE - LUG_F_MIN_Q16);
    uint32_t rate_down = span / LUG_RAMP_DOWN_MS;
    uint32_t rate_up = span / LUG_RAMP_UP_MS;
    if (rate_down == 0)
        rate_down = 1;
    if (rate_up == 0)
        rate_up = 1;

    if (dt > 0)
    {
        if (lug_target > lug_factor)
        {
            uint32_t delta = rate_up * dt;
            uint32_t next = (uint32_t)lug_factor + delta;
            if (next > lug_target)
                next = lug_target;
            lug_factor = clamp_u16(next, 0, Q16_ONE);
        }
        else if (lug_target < lug_factor)
        {
            uint32_t delta = rate_down * dt;
            if (delta > lug_factor)
                lug_factor = lug_target;
            else
            {
                uint32_t next = (uint32_t)lug_factor - delta;
                if (next < lug_target)
                    next = lug_target;
                lug_factor = (uint16_t)next;
            }
        }
    }
    g_power_policy.lug_factor_q16 = lug_factor;
    g_power_policy.p_lug_w = apply_q16(p_user_w, lug_factor);

    /* ---- Thermal governor (I²t model or direct temp) ---- */
    int32_t i_phase = 0;
    if ((g_input_caps & INPUT_CAP_BATT_I) && (g_input_caps & INPUT_CAP_BATT_V))
    {
        int32_t ib = g_inputs.battery_dA;
        if (ib > 0)
        {
            uint32_t denom = (duty_q16 > DUTY_MIN_Q16) ? duty_q16 : DUTY_MIN_Q16;
            i_phase = (int32_t)(((uint32_t)ib << 16) / denom);
        }
    }
    g_power_policy.i_phase_est_dA = clamp_i16(i_phase, 0, 0x7FFF);

    uint16_t thermal_state_u16 = 0;
    uint16_t thermal_factor = Q16_ONE;
    int32_t heat = 0;
    if (i_phase > 0)
        heat = (i_phase * i_phase) >> THERM_STATE_SHIFT;

    g_power_policy.thermal_fast = thermal_step(g_power_policy.thermal_fast, heat, dt, THERM_TAU_FAST_MS);
    g_power_policy.thermal_slow = thermal_step(g_power_policy.thermal_slow, heat, dt, THERM_TAU_SLOW_MS);

    if (g_input_caps & INPUT_CAP_TEMP)
    {
        /* Direct temperature-based limiting */
        int32_t temp = g_inputs.ctrl_temp_dC;
        if (temp <= THERM_TEMP_SOFT_DC)
            thermal_factor = Q16_ONE;
        else if (temp >= THERM_TEMP_HARD_DC)
            thermal_factor = THERM_F_MIN_Q16;
        else
        {
            uint32_t span_t = (uint32_t)(THERM_TEMP_HARD_DC - THERM_TEMP_SOFT_DC);
            uint32_t num_t = (uint32_t)(temp - THERM_TEMP_SOFT_DC) * (uint32_t)(Q16_ONE - THERM_F_MIN_Q16);
            thermal_factor = (uint16_t)(Q16_ONE - (num_t / span_t));
        }
        if (temp < 0)
            temp = 0;
        thermal_state_u16 = clamp_u16((uint32_t)temp, 0, 0xFFFF);
    }
    else if ((g_input_caps & INPUT_CAP_BATT_I) && (g_input_caps & INPUT_CAP_BATT_V))
    {
        /* I²t model based limiting */
        int32_t thermal_state = g_power_policy.thermal_fast;
        if (g_power_policy.thermal_slow > thermal_state)
            thermal_state = g_power_policy.thermal_slow;
        if (thermal_state < 0)
            thermal_state = 0;
        thermal_state_u16 = clamp_u16((uint32_t)thermal_state, 0, 0xFFFF);

        if (thermal_state_u16 <= THERM_HEAT_COOL)
        {
            thermal_factor = Q16_ONE;
        }
        else if (thermal_state_u16 >= THERM_HEAT_HARD)
        {
            thermal_factor = THERM_F_MIN_Q16;
        }
        else
        {
            uint32_t span_h = (uint32_t)(THERM_HEAT_HARD - THERM_HEAT_COOL);
            uint32_t num_h = (uint32_t)(thermal_state_u16 - THERM_HEAT_COOL) * (uint32_t)(Q16_ONE - THERM_F_MIN_Q16);
            thermal_factor = (uint16_t)(Q16_ONE - (num_h / span_h));
        }
    }
    g_power_policy.thermal_state = thermal_state_u16;
    g_power_policy.thermal_factor_q16 = thermal_factor;
    g_power_policy.p_thermal_w = apply_q16(p_user_w, thermal_factor);

    /* ---- Sag governor (battery voltage) ---- */
    uint16_t sag_factor = Q16_ONE;
    int16_t sag_margin = 0;
    if (g_input_caps & INPUT_CAP_BATT_V)
    {
        int32_t v = g_inputs.battery_dV;
        if (v < 0)
            v = 0;
        sag_margin = (int16_t)(v - SAG_START_DV);
        if (v >= SAG_START_DV)
        {
            sag_factor = Q16_ONE;
        }
        else if (v <= SAG_CUTOFF_DV)
        {
            sag_factor = 0;
        }
        else
        {
            uint32_t span_v = (uint32_t)(SAG_START_DV - SAG_CUTOFF_DV);
            uint32_t num_v = (uint32_t)(v - SAG_CUTOFF_DV) * Q16_ONE;
            sag_factor = (uint16_t)(num_v / span_v);
        }
    }
    g_power_policy.sag_margin_dV = sag_margin;
    g_power_policy.p_sag_w = apply_q16(p_user_w, sag_factor);

    /* ---- Final limit: minimum of all governors ---- */
    uint16_t min_p = p_user_w;
    uint8_t reason = LIMIT_REASON_USER;
    if (g_power_policy.p_lug_w < min_p)
    {
        min_p = g_power_policy.p_lug_w;
        reason = LIMIT_REASON_LUG;
    }
    if (g_power_policy.p_thermal_w < min_p)
    {
        min_p = g_power_policy.p_thermal_w;
        reason = LIMIT_REASON_THERM;
    }
    if (g_power_policy.p_sag_w < min_p)
    {
        min_p = g_power_policy.p_sag_w;
        reason = LIMIT_REASON_SAG;
    }
    g_power_policy.p_final_w = min_p;
    g_power_policy.limit_reason = reason;

    /* Log derate events */
    if (reason != LIMIT_REASON_USER && p_user_w > 0)
    {
        if (reason != g_power_policy.last_reason ||
            (now - g_power_policy.last_log_ms) >= LIMIT_LOG_PERIOD_MS)
        {
            event_log_append(EVT_DERATE_ACTIVE, (uint8_t)(reason & 0x0F));
            g_power_policy.last_reason = reason;
            g_power_policy.last_log_ms = now;
        }
    }
    else
    {
        g_power_policy.last_reason = LIMIT_REASON_USER;
    }
}

uint16_t power_policy_final_w(void)
{
    return g_power_policy.p_final_w;
}

limit_reason_t power_policy_limit_reason(void)
{
    return (limit_reason_t)g_power_policy.limit_reason;
}

/* ---- Adaptive assist ---- */
void adaptive_reset(void)
{
    uint16_t speed = g_inputs.speed_dmph;
    g_adapt.speed_ema_dmph = speed;
    g_adapt.speed_delta_dmph = 0;
    g_adapt.power_ema_w = 0;
    g_adapt.eco_output_w = 0;
    g_adapt.last_speed_dmph = speed;
    g_adapt.last_ms = g_ms;
    g_adapt.trend_active = 0;
    g_adapt.eco_clamp_active = 0;
    g_adapt_dt_ms = 0;
    g_adapt_speed_dmph = speed;
}

void adaptive_update(uint16_t speed_dmph, uint16_t power_w, uint32_t now_ms)
{
    uint32_t dt = (g_adapt.last_ms == 0) ? 0u : (now_ms - g_adapt.last_ms);
    g_adapt.last_ms = now_ms;
    g_adapt_dt_ms = dt;
    g_adapt_speed_dmph = speed_dmph;

    if (g_adapt.speed_ema_dmph == 0 || dt == 0)
        g_adapt.speed_ema_dmph = speed_dmph;
    else
        g_adapt.speed_ema_dmph = ema_u16(g_adapt.speed_ema_dmph, speed_dmph, dt, ADAPT_EFFORT_SPEED_TAU_MS);

    int32_t speed_delta = (int32_t)g_adapt.speed_ema_dmph - (int32_t)speed_dmph;
    speed_delta = clamp_i16(speed_delta, -(int32_t)ADAPT_EFFORT_MAX_ERR_DMPH,
                            (int32_t)ADAPT_EFFORT_MAX_ERR_DMPH);
    g_adapt.speed_delta_dmph = (int16_t)speed_delta;

    int32_t power_sample = 0;
    if (power_w > 0)
        power_sample = power_w;
    else if ((g_input_caps & INPUT_CAP_BATT_V) && (g_input_caps & INPUT_CAP_BATT_I))
    {
        int32_t ib = g_inputs.battery_dA;
        int32_t vb = g_inputs.battery_dV;
        if (ib > 0)
        {
            if (vb < 0)
                vb = 0;
            power_sample = (vb * ib + 50) / 100;
        }
    }
    if (power_sample > 0)
    {
        if (g_adapt.power_ema_w == 0 || dt == 0)
            g_adapt.power_ema_w = power_sample;
        else
            g_adapt.power_ema_w = ema_i32(g_adapt.power_ema_w, power_sample, dt, ADAPT_EFFORT_POWER_TAU_MS);
    }
    int32_t power_delta = power_sample - g_adapt.power_ema_w;

    if (speed_delta >= (int32_t)ADAPT_EFFORT_TREND_SPEED_DMPH &&
        power_delta >= (int32_t)ADAPT_EFFORT_TREND_POWER_W)
        g_adapt.trend_active = 1u;
    else
        g_adapt.trend_active = 0u;
}

uint16_t adaptive_effort_boost(uint16_t base_power_w, uint16_t target_speed_dmph)
{
    (void)target_speed_dmph;
    int32_t speed_delta = g_adapt.speed_delta_dmph;
    if (base_power_w < ADAPT_EFFORT_MIN_BASE_W ||
        speed_delta <= (int32_t)ADAPT_EFFORT_MIN_ERR_DMPH)
        return 0;

    int32_t boost = speed_delta * (int32_t)ADAPT_EFFORT_GAIN_W_PER_DMPH;
    if (boost < 0)
        boost = 0;
    if (g_adapt.trend_active)
        boost = (int32_t)(((int32_t)boost * (int32_t)ADAPT_EFFORT_TREND_GAIN_Q15 + (1 << 14)) >> 15);

    uint32_t max_boost = ADAPT_EFFORT_MAX_BOOST_W;
    uint32_t pct_boost = ((uint32_t)base_power_w * (uint32_t)ADAPT_EFFORT_MAX_BOOST_Q15 + (1u << 14)) >> 15;
    if (pct_boost < max_boost)
        max_boost = pct_boost;
    if ((uint32_t)boost > max_boost)
        boost = (int32_t)max_boost;
    return (uint16_t)boost;
}

uint16_t adaptive_eco_limit(uint16_t target_power_w)
{
    uint32_t dt = g_adapt_dt_ms;
    uint16_t speed = g_adapt_speed_dmph;
    uint16_t last = g_adapt.eco_output_w;
    if (last == 0 || dt == 0)
    {
        g_adapt.eco_output_w = target_power_w;
        g_adapt.eco_clamp_active = 0;
        g_adapt.last_speed_dmph = speed;
        return target_power_w;
    }

    int32_t speed_rate = 0;
    if (dt > 0)
        speed_rate = ((int32_t)speed - (int32_t)g_adapt.last_speed_dmph) * 1000 / (int32_t)dt;
    uint32_t rate_wps = ADAPT_ECO_RATE_UP_WPS;
    if (speed_rate > (int32_t)ADAPT_ECO_SPIKE_RATE_DMPH_S)
        rate_wps = ADAPT_ECO_RATE_SPIKE_WPS;
    uint32_t max_rise = (rate_wps * dt + 999u) / 1000u;
    uint32_t allowed = (uint32_t)last + max_rise;
    if (target_power_w > allowed)
    {
        target_power_w = (uint16_t)allowed;
        g_adapt.eco_clamp_active = 1u;
    }
    else
    {
        g_adapt.eco_clamp_active = 0u;
    }
    g_adapt.eco_output_w = target_power_w;
    g_adapt.last_speed_dmph = speed;
    return target_power_w;
}
