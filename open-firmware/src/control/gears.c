/*
 * Virtual Gear System
 *
 * Provides configurable virtual gears that scale motor assist output.
 * Supports linear and exponential gear curves.
 */

#include "control.h"
#include "core/math_util.h"

/* -------------------------------------------------------------
 * Generate gear scale values based on shape
 * ------------------------------------------------------------- */
void vgear_generate_scales(vgear_table_t *t)
{
    if (!t || t->count == 0)
        return;
    uint16_t min = clamp_q15(t->min_scale_q15, VGEAR_SCALE_MIN_Q15, 65535u);
    uint16_t max = clamp_q15(t->max_scale_q15, min, 65535u);
    if (t->count == 1)
    {
        t->scales[0] = min;
        return;
    }
    for (uint8_t i = 0; i < t->count; ++i)
    {
        if (t->shape == VGEAR_SHAPE_EXP)
        {
            /* Quadratic-ish growth: i^2 over (n-1)^2. */
            uint32_t num = (uint32_t)(max - min) * (uint32_t)i * (uint32_t)i;
            uint32_t den = (uint32_t)(t->count - 1u) * (uint32_t)(t->count - 1u);
            t->scales[i] = (uint16_t)(min + num / den);
        }
        else
        {
            /* Linear step between min..max. */
            uint32_t num = (uint32_t)(max - min) * (uint32_t)i;
            uint32_t den = (uint32_t)(t->count - 1u);
            t->scales[i] = (uint16_t)(min + num / den);
        }
    }
}

/* -------------------------------------------------------------
 * Validate gear table
 * ------------------------------------------------------------- */
int vgear_validate(const vgear_table_t *t)
{
    if (!t)
        return 0;
    if (t->count == 0 || t->count > VGEAR_MAX)
        return 0;
    if (t->min_scale_q15 < VGEAR_SCALE_MIN_Q15 || t->min_scale_q15 > 65535u)
        return 0;
    if (t->max_scale_q15 < t->min_scale_q15 || t->max_scale_q15 > 65535u)
        return 0;
    if (t->shape > VGEAR_SHAPE_EXP)
        return 0;
    for (uint8_t i = 0; i < t->count; ++i)
    {
        if (t->scales[i] < VGEAR_SCALE_MIN_Q15 || t->scales[i] > 65535u)
            return 0;
    }
    return 1;
}

/* -------------------------------------------------------------
 * Initialize gear table to defaults
 * ------------------------------------------------------------- */
void vgear_defaults(void)
{
    g_vgears.count = 6;
    g_vgears.shape = VGEAR_SHAPE_LINEAR;
    g_vgears.min_scale_q15 = 24576u; /* 0.75x */
    g_vgears.max_scale_q15 = 49152u; /* 1.50x */
    vgear_generate_scales(&g_vgears);
    g_active_vgear = 1;
}

/* -------------------------------------------------------------
 * Convert Q15 scale to percentage
 * ------------------------------------------------------------- */
uint16_t vgear_q15_to_pct(uint16_t q15)
{
    return (uint16_t)(((uint32_t)q15 * 100u + 16384u) >> 15);
}

/* -------------------------------------------------------------
 * Adjust min scale with step and direction
 * ------------------------------------------------------------- */
void vgear_adjust_min(int dir, uint16_t step)
{
    int32_t v = (int32_t)g_vgears.min_scale_q15 + dir * (int32_t)step;
    if (v < (int32_t)VGEAR_SCALE_MIN_Q15)
        v = VGEAR_SCALE_MIN_Q15;
    if (v > (int32_t)g_vgears.max_scale_q15)
        v = g_vgears.max_scale_q15;
    g_vgears.min_scale_q15 = (uint16_t)v;
    vgear_generate_scales(&g_vgears);
}

/* -------------------------------------------------------------
 * Adjust max scale with step and direction
 * ------------------------------------------------------------- */
void vgear_adjust_max(int dir, uint16_t step)
{
    int32_t v = (int32_t)g_vgears.max_scale_q15 + dir * (int32_t)step;
    if (v < (int32_t)g_vgears.min_scale_q15)
        v = g_vgears.min_scale_q15;
    if (v > (int32_t)VGEAR_SCALE_MAX_Q15)
        v = VGEAR_SCALE_MAX_Q15;
    g_vgears.max_scale_q15 = (uint16_t)v;
    vgear_generate_scales(&g_vgears);
}

/* -------------------------------------------------------------
 * Cadence bias defaults
 * ------------------------------------------------------------- */
void cadence_bias_defaults(void)
{
    g_cadence_bias.enabled = 0;
    g_cadence_bias.target_rpm = 80;
    g_cadence_bias.band_rpm = 20;
    g_cadence_bias.min_bias_q15 = 24576u; /* 0.75x floor */
}
