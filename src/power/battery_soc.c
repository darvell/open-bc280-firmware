#include "battery_soc.h"

#define BATTERY_SOC_CURVE_POINTS 13u

/*
 * These tables are derived from OEM BC280 app v2.5.1 constant blob around 0x80266xx.
 * Keep values identical to preserve on-screen SOC behavior.
 *
 * Note: pct_x100[0] is a sentinel (OEM stores 42000 here due to table packing);
 * the algorithm returns 100% early for segment i==0, so pct_x100[0] is not used.
 */
static const uint16_t k_pct_x100[BATTERY_SOC_CURVE_POINTS] = {
    42000u, 10000u, 9000u, 7500u, 6000u, 4500u, 3692u,
    3115u, 2000u, 1000u, 800u, 500u, 0u
};

static const uint16_t k_curve_24_mv[BATTERY_SOC_CURVE_POINTS] = {
    0u, 29000u, 27700u, 27000u, 26300u, 25600u, 25200u,
    25000u, 24500u, 24200u, 23800u, 23100u, 21000u
};

static const uint16_t k_curve_36_mv[BATTERY_SOC_CURVE_POINTS] = {
    0u, 40800u, 39500u, 38500u, 37500u, 36500u, 36000u,
    35600u, 35000u, 34500u, 34000u, 33000u, 31500u
};

static const uint16_t k_curve_48_mv[BATTERY_SOC_CURVE_POINTS] = {
    0u, 53800u, 51400u, 50100u, 48800u, 47500u, 46800u,
    46300u, 45500u, 44900u, 44200u, 42900u, 42000u
};

static const uint16_t *curve_for_nominal(uint8_t nominal_v, uint32_t batt_mv)
{
    switch (nominal_v)
    {
        case 24u: return k_curve_24_mv;
        case 36u: return k_curve_36_mv;
        case 48u: return k_curve_48_mv;
        default:
            /* OEM has an explicit n48 config. If we don't have it, infer. */
            if (batt_mv >= 42000u) return k_curve_48_mv;
            if (batt_mv >= 30000u) return k_curve_36_mv;
            return k_curve_24_mv;
    }
}

uint8_t battery_soc_pct_from_mv(uint32_t batt_mv, uint8_t nominal_v)
{
    if (batt_mv == 0u)
        return 0u;

    const uint16_t *curve = curve_for_nominal(nominal_v, batt_mv);

    /* Find i such that curve[i+1] <= batt_mv (descending curve points). */
    uint8_t i = 0u;
    for (; i < BATTERY_SOC_CURVE_POINTS - 1u; ++i)
    {
        if ((uint32_t)curve[i + 1u] <= batt_mv)
            break;
    }
    if (i >= BATTERY_SOC_CURVE_POINTS - 1u)
        return 0u;
    if (i == 0u)
        return 100u;

    uint32_t x0 = curve[i];
    uint32_t x1 = curve[i + 1u];
    uint32_t y0 = k_pct_x100[i];
    uint32_t y1 = k_pct_x100[i + 1u];
    if (x0 <= x1)
        return 0u;

    uint32_t dx = x0 - x1;
    uint32_t dy = (y0 >= y1) ? (y0 - y1) : 0u;
    uint32_t x = (batt_mv > x1) ? (batt_mv - x1) : 0u;

    /* Linear interpolation: y = y1 + dy * x / dx. Percent is y/100. */
    uint32_t y = y1;
    if (dy && x)
        y = y1 + (dy * x) / dx;

    uint32_t pct = (y + 50u) / 100u; /* round to nearest percent */
    if (pct > 100u)
        pct = 100u;
    return (uint8_t)pct;
}
