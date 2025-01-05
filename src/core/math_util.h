#ifndef MATH_UTIL_H
#define MATH_UTIL_H

#include <stdint.h>
#include <limits.h>

/* Clamping utilities */
static inline uint16_t clamp_q15(uint16_t v, uint16_t mn, uint16_t mx)
{
    if (v < mn) return mn;
    if (v > mx) return mx;
    return v;
}

static inline uint16_t clamp_u16(uint32_t v, uint16_t mn, uint16_t mx)
{
    if (v < mn) return mn;
    if (v > mx) return mx;
    return (uint16_t)v;
}

static inline int16_t clamp_i16(int32_t v, int16_t mn, int16_t mx)
{
    if (v < mn) return mn;
    if (v > mx) return mx;
    return (int16_t)v;
}

/* Q16 fixed-point multiplication */
static inline uint16_t apply_q16(uint16_t v, uint16_t q16)
{
    return (uint16_t)(((uint32_t)v * (uint32_t)q16 + 0x8000u) >> 16);
}

/* Thermal/exponential step with time constant */
static inline int32_t thermal_step(int32_t state, int32_t heat, uint32_t dt_ms, uint32_t tau_ms)
{
    if (tau_ms == 0)
        return heat;
    int32_t diff = heat - state;
    int32_t delta = diff * (int32_t)dt_ms;
    state += delta / (int32_t)tau_ms;
    return state;
}

/* Exponential moving average (unsigned 16-bit) */
static inline uint16_t ema_u16(uint16_t state, uint16_t sample, uint32_t dt_ms, uint32_t tau_ms)
{
    if (tau_ms == 0 || dt_ms == 0)
        return sample;
    if (dt_ms >= tau_ms)
        return sample;
    int32_t diff = (int32_t)sample - (int32_t)state;
    int32_t delta = (diff * (int32_t)dt_ms) / (int32_t)tau_ms;
    int32_t next = (int32_t)state + delta;
    if (next < 0)
        next = 0;
    if (next > 0xFFFF)
        next = 0xFFFF;
    return (uint16_t)next;
}

/* Exponential moving average (signed 32-bit) */
static inline int32_t ema_i32(int32_t state, int32_t sample, uint32_t dt_ms, uint32_t tau_ms)
{
    if (tau_ms == 0 || dt_ms == 0)
        return sample;
    if (dt_ms >= tau_ms)
        return sample;
    int32_t diff = sample - state;
    int32_t delta = (diff * (int32_t)dt_ms) / (int32_t)tau_ms;
    int32_t next = state + delta;
    return next;
}

/* Integer division for 64-bit numerator (manual implementation to avoid libgcc) */
static inline uint32_t divu64_32(uint64_t n, uint32_t d)
{
    if (d == 0)
        return 0xFFFFFFFFu;

    uint64_t rem = 0;
    uint32_t q = 0;
    for (int i = 63; i >= 0; --i)
    {
        rem = (rem << 1) | ((n >> i) & 1ull);
        if (rem >= d)
        {
            rem -= d;
            q |= (uint32_t)(1ull << i);
        }
    }
    return q;
}

#endif /* MATH_UTIL_H */
