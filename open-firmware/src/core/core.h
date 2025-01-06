#ifndef CORE_H
#define CORE_H

#include <stdint.h>
#include <stddef.h>

/* -------------------------------------------------------------
 * Fixed-point helpers (no floats)
 * ------------------------------------------------------------- */

/* Q15 multiply with rounding. */
static inline int32_t fxp_mul_q15(int32_t a, int32_t b)
{
    return (int32_t)((a * b + (1 << 14)) >> 15);
}

/* Convert millivolts to decivolts (0.1 V). */
static inline int16_t fxp_millivolts_to_decivolts(int32_t mv)
{
    return (int16_t)((mv + 50) / 100); /* round to nearest 0.1 V */
}

/* Convert milliamps to deciamps (0.1 A). */
static inline int16_t fxp_milliamps_to_deciamperes(int32_t ma)
{
    return (int16_t)((ma + 50) / 100);
}

/* Compute watts from millivolts * milliamps using integers. */
static inline int32_t fxp_watts_from_mv_ma(int32_t mv, int32_t ma)
{
    /* (mv * ma) / 1e6 with rounding */
    return (mv * ma + 500000) / 1000000;
}

/* Convert meters-per-second (scaled by 1e3) to deci-mph (0.1 mph). */
static inline int16_t fxp_mps1000_to_dmph(int32_t mps_x1000)
{
    /* 1 m/s = 2.23694 mph -> 22.3694 deci-mph. Scale by 1000. */
    const int64_t num = 223694;   /* deci-mph * 1e4 per m/s */
    const int64_t denom = 100000; /* accounts for x1000 input */
    return (int16_t)((mps_x1000 * num + (denom / 2)) / denom);
}

/* Simple piecewise-linear interpolation for bounded arrays. */
typedef struct {
    int32_t x;
    int32_t y;
} fxp_point_t;

static inline int32_t fxp_interp_linear(int32_t x, const fxp_point_t *pts, size_t count)
{
    if (!pts || count == 0)
        return 0;
    if (count == 1 || x <= pts[0].x)
        return pts[0].y;
    if (x >= pts[count - 1].x)
        return pts[count - 1].y;
    for (size_t i = 1; i < count; ++i)
    {
        if (x <= pts[i].x)
        {
            int32_t x0 = pts[i - 1].x;
            int32_t y0 = pts[i - 1].y;
            int32_t x1 = pts[i].x;
            int32_t y1 = pts[i].y;
            int32_t dx = x1 - x0;
            if (dx == 0)
                return y0;
            int32_t dy = y1 - y0;
            int32_t num = (x - x0) * dy;
            return y0 + num / dx;
        }
    }
    return pts[count - 1].y;
}

/* -------------------------------------------------------------
 * Ring buffer with O(1) min/max over the active window.
 * ------------------------------------------------------------- */

typedef struct {
    uint16_t *buf;
    uint16_t head;
    uint16_t tail;
    uint16_t capacity; /* matches parent ring capacity */
    uint16_t count;
} mono_queue_t;

typedef struct {
    int16_t *data;
    uint16_t capacity; /* must be power-of-two */
    uint16_t mask;
    uint16_t count;
    uint32_t head;     /* monotonic write index */
    mono_queue_t min_q;
    mono_queue_t max_q;
} ringbuf_i16_t;

typedef struct {
    uint16_t count;
    uint16_t capacity;
    int16_t min;
    int16_t max;
    int16_t latest;
} ringbuf_i16_summary_t;

void ringbuf_i16_init(ringbuf_i16_t *rb, int16_t *storage, uint16_t capacity,
                      uint16_t *min_idx_buf, uint16_t *max_idx_buf);
void ringbuf_i16_reset(ringbuf_i16_t *rb);
void ringbuf_i16_push(ringbuf_i16_t *rb, int16_t sample);
void ringbuf_i16_summary(const ringbuf_i16_t *rb, ringbuf_i16_summary_t *out);

#endif /* CORE_H */
