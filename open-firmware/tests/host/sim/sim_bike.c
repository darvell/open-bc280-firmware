#include "sim_bike.h"

void sim_bike_init(sim_bike_t *b)
{
    if (!b)
        return;
    b->t_ms = 0;
    b->speed_dmph = 0;
    b->rpm = 0;
    b->cadence_rpm = 0;
    b->torque_raw = 0;
    b->power_w = 0;
    b->batt_dV = 520;
    b->batt_dA = 0;
    b->soc_pct = 90;
    b->err = 0;
}

static uint16_t speed_profile(uint32_t t_s)
{
    if (t_s < 10)
        return (uint16_t)(t_s * 25u); /* 0..25 mph */
    if (t_s < 20)
        return 250u;
    if (t_s < 30)
        return (uint16_t)(250u - (t_s - 20u) * 25u);
    return 0;
}

void sim_bike_step(sim_bike_t *b, uint32_t dt_ms)
{
    if (!b)
        return;
    b->t_ms += dt_ms;
    uint32_t t_s = b->t_ms / 1000u;
    b->speed_dmph = speed_profile(t_s);
    b->rpm = (uint16_t)(b->speed_dmph * 3u);

    b->cadence_rpm = (uint16_t)((b->speed_dmph * 4u) / 10u + 60u);
    b->torque_raw = (uint16_t)(20u + (b->speed_dmph / 5u));
    b->power_w = (uint16_t)((b->speed_dmph * b->torque_raw) / 80u);

    int16_t sag = (int16_t)(b->power_w / 40u);
    b->batt_dV = (int16_t)(520 - sag);
    if (b->batt_dV < 440)
        b->batt_dV = 440;

    b->batt_dA = (int16_t)(b->power_w ? (b->power_w * 10 / b->batt_dV) : 0);
    if (b->t_ms % 5000u == 0 && b->soc_pct > 1)
        b->soc_pct--;
}
