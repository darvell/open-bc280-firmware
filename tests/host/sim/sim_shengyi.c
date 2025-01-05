#include "sim_shengyi.h"

#include <stdlib.h>

static double clampd(double v, double lo, double hi)
{
    if (v < lo)
        return lo;
    if (v > hi)
        return hi;
    return v;
}

static double env_or(const char *name, double def)
{
    const char *v = getenv(name);
    if (!v || !*v)
        return def;
    char *end = NULL;
    double out = strtod(v, &end);
    return (end && end != v) ? out : def;
}

static uint8_t env_u8_or(const char *name, uint8_t def)
{
    const char *v = getenv(name);
    if (!v || !*v)
        return def;
    char *end = NULL;
    long out = strtol(v, &end, 10);
    if (end && end != v && out >= 0 && out <= 255)
        return (uint8_t)out;
    return def;
}

void sim_shengyi_init(sim_shengyi_t *s)
{
    if (!s)
        return;
    s->mass_kg = env_or("BC280_SIM_MASS_KG", 95.0);
    s->crr = env_or("BC280_SIM_CRR", 0.010);
    s->cda = env_or("BC280_SIM_CDA", 0.55);
    s->grade = env_or("BC280_SIM_GRADE", 0.0);
    s->wind_mps = env_or("BC280_SIM_WIND_MPS", 0.0);
    s->wheel_radius_m = env_or("BC280_SIM_WHEEL_R", 0.34);
    s->eff = env_or("BC280_SIM_EFF", 0.85);
    s->assist_level = env_u8_or("BC280_SIM_ASSIST", 2);
    s->t_ms = 0;
    s->v_mps = 0.0;
    s->cadence_rpm = 0.0;
    s->rider_power_w = 100.0;
    s->motor_power_w = 0.0;
    s->batt_v = 52.0;
    s->batt_a = 0.0;
    s->temp_c = 30.0;
    s->soc_pct = 90;
    s->torque_raw = 30;
    s->err = 0;
}

static double assist_factor(uint8_t level)
{
    static const double map[] = {0.0, 0.5, 1.0, 1.4, 1.8};
    if (level >= (sizeof(map) / sizeof(map[0])))
        level = (uint8_t)(sizeof(map) / sizeof(map[0]) - 1u);
    return map[level];
}

void sim_shengyi_step(sim_shengyi_t *s, uint32_t dt_ms)
{
    if (!s || dt_ms == 0)
        return;
    s->t_ms += dt_ms;
    const double dt = (double)dt_ms / 1000.0;

    /* Scripted rider power: ramp up then down. */
    double t = dt;
    (void)t;
    if (s->rider_power_w < 220.0)
        s->rider_power_w += 6.0 * dt_ms / 200.0;
    else if (s->rider_power_w > 120.0 && s->v_mps > 6.0)
        s->rider_power_w -= 2.5 * dt_ms / 200.0;

    double a_fac = assist_factor(s->assist_level);
    s->motor_power_w = s->rider_power_w * a_fac;

    double v_rel = s->v_mps - s->wind_mps;
    double f_drag = 0.5 * 1.225 * s->cda * v_rel * v_rel;
    double f_roll = s->mass_kg * 9.81 * s->crr;
    double f_grade = s->mass_kg * 9.81 * s->grade;

    double p_total = (s->rider_power_w + s->motor_power_w) * s->eff;
    double f_prop = (s->v_mps > 0.5) ? (p_total / s->v_mps) : p_total / 0.5;
    double f_net = f_prop - f_drag - f_roll - f_grade;
    double a = f_net / s->mass_kg;

    s->v_mps = clampd(s->v_mps + a * dt, 0.0, 20.0);
    s->cadence_rpm = clampd((s->v_mps / (2.0 * 3.14159 * s->wheel_radius_m)) * 60.0 * 2.6, 40.0, 110.0);
    s->torque_raw = (uint16_t)clampd(s->rider_power_w / (s->cadence_rpm * 0.1047), 10.0, 120.0);

    double batt_power = s->motor_power_w / s->eff;
    s->batt_a = (s->batt_v > 1.0) ? (batt_power / s->batt_v) : 0.0;
    s->batt_v = clampd(52.0 - s->batt_a * 0.05, 44.0, 54.6);
    if ((s->t_ms % 5000u) == 0 && s->soc_pct > 1)
        s->soc_pct--;
}

uint16_t sim_shengyi_speed_dmph(const sim_shengyi_t *s)
{
    double mph = s->v_mps * 2.23694;
    if (mph < 0.0)
        mph = 0.0;
    return (uint16_t)(mph * 10.0 + 0.5);
}

uint16_t sim_shengyi_cadence_rpm(const sim_shengyi_t *s)
{
    return (uint16_t)(s->cadence_rpm + 0.5);
}

uint16_t sim_shengyi_power_w(const sim_shengyi_t *s)
{
    return (uint16_t)(s->motor_power_w + s->rider_power_w + 0.5);
}

int16_t sim_shengyi_batt_dV(const sim_shengyi_t *s)
{
    return (int16_t)(s->batt_v * 10.0 + 0.5);
}

int16_t sim_shengyi_batt_dA(const sim_shengyi_t *s)
{
    return (int16_t)(s->batt_a * 10.0 + 0.5);
}
