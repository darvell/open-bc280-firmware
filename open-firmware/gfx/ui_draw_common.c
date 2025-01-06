#include "ui_draw_common.h"

#include "ui_color.h"
#include "ui_display.h"
#include "src/core/trace_format.h"

#include "ui_trig.h"

static const uint8_t k_dither_4x4[16] = {
    0u,  8u,  2u, 10u,
    12u, 4u, 14u, 6u,
    3u, 11u, 1u,  9u,
    15u, 7u, 13u, 5u
};

uint16_t ui_draw_dither_pick(uint16_t x, uint16_t y, uint16_t c0, uint16_t c1, uint8_t level)
{
    uint8_t t = k_dither_4x4[((y & 3u) << 2) | (x & 3u)];
    return (t < level) ? c1 : c0;
}

static uint16_t isqrt_u32(uint32_t n)
{
    /* Integer sqrt (floor). n is small (<= r^2) so a simple loop is fine. */
    uint32_t r = 0;
    while ((r + 1u) * (r + 1u) <= n)
        r++;
    return (uint16_t)r;
}

void ui_draw_format_value(char *out, size_t len, const char *label, long value)
{
    if (!out || len == 0)
        return;
    char *ptr = out;
    size_t rem = len - 1u;
    if (label && label[0])
    {
        append_str(&ptr, &rem, label);
        if (rem)
            append_char(&ptr, &rem, ' ');
    }

    long v = value;
    if (v < 0)
    {
        append_char(&ptr, &rem, '-');
        v = -v;
    }
    append_u32(&ptr, &rem, (uint32_t)v);
    *ptr = 0;
}

void ui_draw_fill_round_rect(const ui_draw_rect_ops_t *ops, void *ctx, uint16_t x, uint16_t y,
                             uint16_t w, uint16_t h, uint16_t color, uint8_t radius)
{
    if (!ops || !ops->fill_hline || !ops->fill_rect)
        return;
    if (w == 0u || h == 0u)
        return;

    uint8_t r = radius;
    if (r == 0u || w <= (uint16_t)(2u * r) || h <= (uint16_t)(2u * r))
    {
        ops->fill_rect(ctx, x, y, w, h, color);
        return;
    }

    const int rr = (int)r;
    const uint32_t rr2 = (uint32_t)rr * (uint32_t)rr;

    for (uint16_t dy = 0; dy < r; ++dy)
    {
        int yy = (rr - 1) - (int)dy;
        uint32_t xx2 = rr2 - (uint32_t)(yy * yy);
        int xx = (int)isqrt_u32(xx2);
        int inset = (rr - 1) - xx;
        if (inset < 0)
            inset = 0;
        uint16_t span_w = (uint16_t)(w - 2u * (uint16_t)inset);
        ops->fill_hline(ctx, (uint16_t)(x + (uint16_t)inset), (uint16_t)(y + dy), span_w, color);
    }

    uint16_t mid_h = (h > (uint16_t)(2u * r)) ? (uint16_t)(h - 2u * (uint16_t)r) : 0u;
    if (mid_h)
        ops->fill_rect(ctx, x, (uint16_t)(y + r), w, mid_h, color);

    for (uint16_t dy = 0; dy < r; ++dy)
    {
        uint16_t yrow = (uint16_t)(h - r + dy);
        int yy = (rr - 1) - (int)dy;
        uint32_t xx2 = rr2 - (uint32_t)(yy * yy);
        int xx = (int)isqrt_u32(xx2);
        int inset = (rr - 1) - xx;
        if (inset < 0)
            inset = 0;
        uint16_t span_w = (uint16_t)(w - 2u * (uint16_t)inset);
        ops->fill_hline(ctx, (uint16_t)(x + (uint16_t)inset), (uint16_t)(y + yrow), span_w, color);
    }
}

void ui_draw_fill_round_rect_dither(const ui_draw_rect_ops_t *ops, void *ctx, uint16_t x, uint16_t y,
                                    uint16_t w, uint16_t h, uint16_t color, uint16_t alt,
                                    uint8_t radius, uint8_t level)
{
    if (!ops || !ops->fill_hline_dither || !ops->fill_rect_dither)
        return;
    if (w == 0u || h == 0u)
        return;

    if (level == 0u || color == alt)
    {
        ui_draw_fill_round_rect(ops, ctx, x, y, w, h, color, radius);
        return;
    }
    if (level >= 16u)
    {
        ui_draw_fill_round_rect(ops, ctx, x, y, w, h, alt, radius);
        return;
    }

    uint8_t r = radius;
    if (r == 0u || w <= (uint16_t)(2u * r) || h <= (uint16_t)(2u * r))
    {
        ops->fill_rect_dither(ctx, x, y, w, h, color, alt, level);
        return;
    }

    const int rr = (int)r;
    const uint32_t rr2 = (uint32_t)rr * (uint32_t)rr;

    for (uint16_t dy = 0; dy < r; ++dy)
    {
        int yy = (rr - 1) - (int)dy;
        uint32_t xx2 = rr2 - (uint32_t)(yy * yy);
        int xx = (int)isqrt_u32(xx2);
        int inset = (rr - 1) - xx;
        if (inset < 0)
            inset = 0;
        uint16_t span_w = (uint16_t)(w - 2u * (uint16_t)inset);
        ops->fill_hline_dither(ctx, (uint16_t)(x + (uint16_t)inset), (uint16_t)(y + dy), span_w, color, alt, level);
    }

    uint16_t mid_h = (h > (uint16_t)(2u * r)) ? (uint16_t)(h - 2u * (uint16_t)r) : 0u;
    if (mid_h)
        ops->fill_rect_dither(ctx, x, (uint16_t)(y + r), w, mid_h, color, alt, level);

    for (uint16_t dy = 0; dy < r; ++dy)
    {
        uint16_t yrow = (uint16_t)(h - r + dy);
        int yy = (rr - 1) - (int)dy;
        uint32_t xx2 = rr2 - (uint32_t)(yy * yy);
        int xx = (int)isqrt_u32(xx2);
        int inset = (rr - 1) - xx;
        if (inset < 0)
            inset = 0;
        uint16_t span_w = (uint16_t)(w - 2u * (uint16_t)inset);
        ops->fill_hline_dither(ctx, (uint16_t)(x + (uint16_t)inset), (uint16_t)(y + yrow), span_w, color, alt, level);
    }
}

void ui_draw_big_digit_7seg(const ui_draw_rect_ops_t *ops, void *ctx, uint16_t x, uint16_t y,
                            uint8_t digit, uint8_t scale, uint16_t color)
{
    static const uint8_t segs[10] = {
        0x3Fu, 0x06u, 0x5Bu, 0x4Fu, 0x66u, 0x6Du, 0x7Du, 0x07u, 0x7Fu, 0x6Fu
    };
    uint8_t s = (digit < 10u) ? segs[digit] : 0u;

    int thick = (3 * (int)scale) / 2 + 1;
    int w = 12 * (int)scale;
    int h = 20 * (int)scale;
    uint8_t rad = (uint8_t)((thick > 2) ? (thick / 2) : 1);

    uint16_t x0 = x;
    uint16_t y0 = y;

    if (s & 0x01u)
        ui_draw_fill_round_rect(ops, ctx, (uint16_t)(x0 + thick), y0, (uint16_t)(w - 2 * thick),
                                (uint16_t)thick, color, rad);
    if (s & 0x02u)
        ui_draw_fill_round_rect(ops, ctx, (uint16_t)(x0 + w - thick), (uint16_t)(y0 + thick),
                                (uint16_t)thick, (uint16_t)(h / 2 - thick), color, rad);
    if (s & 0x04u)
        ui_draw_fill_round_rect(ops, ctx, (uint16_t)(x0 + w - thick), (uint16_t)(y0 + h / 2),
                                (uint16_t)thick, (uint16_t)(h / 2 - thick), color, rad);
    if (s & 0x08u)
        ui_draw_fill_round_rect(ops, ctx, (uint16_t)(x0 + thick), (uint16_t)(y0 + h - thick),
                                (uint16_t)(w - 2 * thick), (uint16_t)thick, color, rad);
    if (s & 0x10u)
        ui_draw_fill_round_rect(ops, ctx, x0, (uint16_t)(y0 + h / 2), (uint16_t)thick,
                                (uint16_t)(h / 2 - thick), color, rad);
    if (s & 0x20u)
        ui_draw_fill_round_rect(ops, ctx, x0, (uint16_t)(y0 + thick), (uint16_t)thick,
                                (uint16_t)(h / 2 - thick), color, rad);
    if (s & 0x40u)
        ui_draw_fill_round_rect(ops, ctx, (uint16_t)(x0 + thick), (uint16_t)(y0 + h / 2 - thick / 2),
                                (uint16_t)(w - 2 * thick), (uint16_t)thick, color, rad);
}

void ui_draw_battery_icon_ops(const ui_draw_rect_ops_t *ops, void *ctx, uint16_t x, uint16_t y,
                          uint16_t w, uint16_t h, uint8_t soc, uint16_t color, uint16_t bg)
{
    if (!ops || !ops->fill_rect)
        return;
    if (w < 8u || h < 6u)
        return;
    if (soc > 100u)
        soc = 100u;

    uint16_t cap_w = (uint16_t)(w / 8u);
    uint16_t body_w = (uint16_t)(w - cap_w - 2u);
    uint16_t outline = rgb565_dim(color);
    uint16_t t = 2u;
    uint8_t rad = (uint8_t)(h / 3u);
    if (rad > 6u)
        rad = 6u;

    ui_draw_fill_round_rect(ops, ctx, x, y, body_w, h, outline, rad);
    ui_draw_fill_round_rect(ops, ctx, (uint16_t)(x + body_w + 2u), (uint16_t)(y + h / 3u),
                            cap_w, (uint16_t)(h / 3u), outline, (uint8_t)(rad > 2u ? rad - 2u : 1u));

    if (body_w > 2u * t && h > 2u * t)
        ui_draw_fill_round_rect(ops, ctx, (uint16_t)(x + t), (uint16_t)(y + t),
                                (uint16_t)(body_w - 2u * t), (uint16_t)(h - 2u * t),
                                bg, (uint8_t)(rad > 2u ? rad - 2u : 1u));

    uint16_t inner_w = (body_w > 2u * t) ? (uint16_t)(body_w - 2u * t) : 0u;
    uint16_t inner_h = (h > 2u * t) ? (uint16_t)(h - 2u * t) : 0u;
    uint16_t fill_w = (uint16_t)((uint32_t)inner_w * (uint32_t)soc / 100u);
    if (fill_w && inner_h)
        ops->fill_rect(ctx, (uint16_t)(x + t), (uint16_t)(y + t), fill_w, inner_h, color);
}

void ui_draw_warning_icon_ops(const ui_draw_rect_ops_t *ops, void *ctx, uint16_t x, uint16_t y, uint16_t color)
{
    if (!ops || !ops->fill_rect)
        return;
    ui_draw_fill_round_rect(ops, ctx, x, y, 12u, 12u, color, 3u);
    ops->fill_rect(ctx, (uint16_t)(x + 5u), (uint16_t)(y + 3u), 2u, 6u, 0x0000u);
    ops->fill_rect(ctx, (uint16_t)(x + 5u), (uint16_t)(y + 10u), 2u, 2u, 0x0000u);
}

static uint16_t blend_rgb565(uint16_t bg, uint16_t fg, uint8_t a4)
{
    if (a4 == 0u)
        return bg;
    if (a4 >= 15u)
        return fg;

    uint8_t br = (bg >> 11) & 0x1Fu;
    uint8_t bgc = (bg >> 5) & 0x3Fu;
    uint8_t bb = bg & 0x1Fu;
    uint8_t fr = (fg >> 11) & 0x1Fu;
    uint8_t fgc = (fg >> 5) & 0x3Fu;
    uint8_t fb = fg & 0x1Fu;

    uint8_t inv = (uint8_t)(15u - a4);
    uint8_t r = (uint8_t)((fr * a4 + br * inv + 7u) / 15u);
    uint8_t g = (uint8_t)((fgc * a4 + bgc * inv + 7u) / 15u);
    uint8_t b = (uint8_t)((fb * a4 + bb * inv + 7u) / 15u);
    return (uint16_t)((r << 11) | (g << 5) | b);
}

static uint8_t a4_from_sd_half(int32_t sd_half, int32_t aa_half)
{
    if (aa_half <= 0)
        return (sd_half < 0) ? 15u : 0u;
    if (sd_half <= -aa_half)
        return 15u;
    if (sd_half >= aa_half)
        return 0u;
    int32_t den = 2 * aa_half;
    int32_t num = (aa_half - sd_half) * 15;
    if (num <= 0)
        return 0u;
    if (num >= den * 15)
        return 15u;
    return (uint8_t)((num + den / 2) / den);
}

static int arc_contains_cw(int32_t px, int32_t py, ui_vec2_i16_t s_q15, ui_vec2_i16_t e_q15, uint16_t sweep_deg)
{
    if (sweep_deg >= 360u)
        return 1;
    if (sweep_deg == 0u)
        return 0;

    int32_t c1 = (int32_t)s_q15.x * py - (int32_t)s_q15.y * px; /* cross(S, P) */
    int32_t c2 = (int32_t)px * e_q15.y - (int32_t)py * e_q15.x; /* cross(P, E) */

    if (sweep_deg <= 180u)
        return (c1 >= 0 && c2 >= 0);

    /* Wide arc: inside = NOT(in the excluded small arc from E->S). */
    int32_t ce1 = (int32_t)e_q15.x * py - (int32_t)e_q15.y * px; /* cross(E, P) */
    int32_t ce2 = (int32_t)px * s_q15.y - (int32_t)py * s_q15.x; /* cross(P, S) */
    return !(ce1 >= 0 && ce2 >= 0);
}

void ui_draw_ring_arc_a4(const ui_draw_pixel_writer_t *ops, void *ctx,
                         uint16_t clip_x, uint16_t clip_y, uint16_t clip_w, uint16_t clip_h,
                         int16_t cx, int16_t cy, uint16_t outer_r, uint16_t thickness,
                         int16_t start_deg_cw, uint16_t sweep_deg_cw,
                         uint16_t fg, uint16_t bg)
{
    if (!ops || !ops->write_pixel)
        return;
    if (outer_r == 0u || thickness == 0u || sweep_deg_cw == 0u)
        return;
    if (clip_w == 0u || clip_h == 0u)
        return;

    if (thickness >= outer_r)
        thickness = outer_r;
    uint16_t inner_r = (uint16_t)(outer_r - thickness);

    int x0 = (int)cx - (int)outer_r - 2;
    int y0 = (int)cy - (int)outer_r - 2;
    int w = 2 * (int)outer_r + 4;
    int h = 2 * (int)outer_r + 4;

    int cx0 = (int)clip_x;
    int cy0 = (int)clip_y;
    int cx1 = cx0 + (int)clip_w;
    int cy1 = cy0 + (int)clip_h;
    int x1 = x0 + w;
    int y1 = y0 + h;
    if (x0 < cx0)
        x0 = cx0;
    if (y0 < cy0)
        y0 = cy0;
    if (x1 > cx1)
        x1 = cx1;
    if (y1 > cy1)
        y1 = cy1;
    w = x1 - x0;
    h = y1 - y0;

    if (x0 < 0)
    {
        w += x0;
        x0 = 0;
    }
    if (y0 < 0)
    {
        h += y0;
        y0 = 0;
    }
    if (x0 + w > (int)DISP_W)
        w = (int)DISP_W - x0;
    if (y0 + h > (int)DISP_H)
        h = (int)DISP_H - y0;

    if (w <= 0 || h <= 0)
        return;

    uint16_t sweep = sweep_deg_cw;
    if (sweep > 360u)
        sweep = 360u;

    ui_vec2_i16_t s = ui_trig_unit_deg_cw_q15(start_deg_cw);
    ui_vec2_i16_t e = ui_trig_unit_deg_cw_q15((int16_t)(start_deg_cw + (int16_t)sweep));

    const int32_t outerR = (int32_t)outer_r * 2;
    const int32_t innerR = (int32_t)inner_r * 2;
    const int32_t outerR2 = outerR * outerR;
    const int32_t innerR2 = innerR * innerR;
    const int32_t denom_outer = 2 * outerR;
    const int32_t denom_inner = (innerR > 0) ? (2 * innerR) : 1;
    const int32_t aa_half = 3;

    const int32_t cx2 = (int32_t)cx * 2;
    const int32_t cy2 = (int32_t)cy * 2;

    if (ops->begin_window)
        ops->begin_window(ctx, (uint16_t)x0, (uint16_t)y0, (uint16_t)w, (uint16_t)h);

    for (int yy = 0; yy < h; ++yy)
    {
        int y = y0 + yy;
        int32_t py = (int32_t)y - (int32_t)cy;
        int32_t py2 = (int32_t)(y * 2 + 1) - cy2;
        for (int xx = 0; xx < w; ++xx)
        {
            int x = x0 + xx;
            int32_t px = (int32_t)x - (int32_t)cx;
            int32_t px2 = (int32_t)(x * 2 + 1) - cx2;
            int32_t dist2 = px2 * px2 + py2 * py2;

            uint8_t a4 = 0u;
            if (sweep == 360u || arc_contains_cw(px, py, s, e, sweep))
            {
                int32_t sd_outer = (dist2 - outerR2) / denom_outer;
                uint8_t a_outer = a4_from_sd_half(sd_outer, aa_half);
                uint8_t a_inner = 15u;
                if (innerR > 0)
                {
                    int32_t sd_inner = -(dist2 - innerR2) / denom_inner;
                    a_inner = a4_from_sd_half(sd_inner, aa_half);
                }
                a4 = (a_outer < a_inner) ? a_outer : a_inner;
            }

            ops->write_pixel(ctx, (uint16_t)x, (uint16_t)y, blend_rgb565(bg, fg, a4));
        }
    }
}

void ui_draw_ring_gauge_a4(const ui_draw_pixel_writer_t *ops, void *ctx,
                           uint16_t clip_x, uint16_t clip_y, uint16_t clip_w, uint16_t clip_h,
                           int16_t cx, int16_t cy, uint16_t outer_r, uint16_t thickness,
                           int16_t start_deg_cw, uint16_t sweep_deg_cw, uint16_t active_sweep_deg_cw,
                           uint16_t fg_active, uint16_t fg_inactive, uint16_t bg)
{
    if (!ops || !ops->write_pixel)
        return;
    if (outer_r == 0u || thickness == 0u || sweep_deg_cw == 0u)
        return;
    if (clip_w == 0u || clip_h == 0u)
        return;

    if (thickness >= outer_r)
        thickness = outer_r;
    uint16_t inner_r = (uint16_t)(outer_r - thickness);

    int x0 = (int)cx - (int)outer_r - 2;
    int y0 = (int)cy - (int)outer_r - 2;
    int w = 2 * (int)outer_r + 4;
    int h = 2 * (int)outer_r + 4;

    int cx0 = (int)clip_x;
    int cy0 = (int)clip_y;
    int cx1 = cx0 + (int)clip_w;
    int cy1 = cy0 + (int)clip_h;
    int x1 = x0 + w;
    int y1 = y0 + h;
    if (x0 < cx0)
        x0 = cx0;
    if (y0 < cy0)
        y0 = cy0;
    if (x1 > cx1)
        x1 = cx1;
    if (y1 > cy1)
        y1 = cy1;
    w = x1 - x0;
    h = y1 - y0;

    if (x0 < 0)
    {
        w += x0;
        x0 = 0;
    }
    if (y0 < 0)
    {
        h += y0;
        y0 = 0;
    }
    if (x0 + w > (int)DISP_W)
        w = (int)DISP_W - x0;
    if (y0 + h > (int)DISP_H)
        h = (int)DISP_H - y0;

    if (w <= 0 || h <= 0)
        return;

    uint16_t sweep = sweep_deg_cw;
    if (sweep > 360u)
        sweep = 360u;

    uint16_t active_sweep = active_sweep_deg_cw;
    if (active_sweep > sweep)
        active_sweep = sweep;

    ui_vec2_i16_t s = ui_trig_unit_deg_cw_q15(start_deg_cw);
    ui_vec2_i16_t e_full = ui_trig_unit_deg_cw_q15((int16_t)(start_deg_cw + (int16_t)sweep));
    ui_vec2_i16_t e_act = ui_trig_unit_deg_cw_q15((int16_t)(start_deg_cw + (int16_t)active_sweep));

    const int32_t outerR = (int32_t)outer_r * 2;
    const int32_t innerR = (int32_t)inner_r * 2;
    const int32_t outerR2 = outerR * outerR;
    const int32_t innerR2 = innerR * innerR;
    const int32_t denom_outer = 2 * outerR;
    const int32_t denom_inner = (innerR > 0) ? (2 * innerR) : 1;
    const int32_t aa_half = 3;

    const int32_t cx2 = (int32_t)cx * 2;
    const int32_t cy2 = (int32_t)cy * 2;

    if (ops->begin_window)
        ops->begin_window(ctx, (uint16_t)x0, (uint16_t)y0, (uint16_t)w, (uint16_t)h);

    for (int yy = 0; yy < h; ++yy)
    {
        int y = y0 + yy;
        int32_t py = (int32_t)y - (int32_t)cy;
        int32_t py2 = (int32_t)(y * 2 + 1) - cy2;
        for (int xx = 0; xx < w; ++xx)
        {
            int x = x0 + xx;
            int32_t px = (int32_t)x - (int32_t)cx;
            int32_t px2 = (int32_t)(x * 2 + 1) - cx2;
            int32_t dist2 = px2 * px2 + py2 * py2;

            uint8_t a4 = 0u;
            uint16_t fg = fg_inactive;
            if (sweep == 360u || arc_contains_cw(px, py, s, e_full, sweep))
            {
                if (active_sweep && arc_contains_cw(px, py, s, e_act, active_sweep))
                    fg = fg_active;

                int32_t sd_outer = (dist2 - outerR2) / denom_outer;
                uint8_t a_outer = a4_from_sd_half(sd_outer, aa_half);
                uint8_t a_inner = 15u;
                if (innerR > 0)
                {
                    int32_t sd_inner = -(dist2 - innerR2) / denom_inner;
                    a_inner = a4_from_sd_half(sd_inner, aa_half);
                }
                a4 = (a_outer < a_inner) ? a_outer : a_inner;
            }

            ops->write_pixel(ctx, (uint16_t)x, (uint16_t)y, blend_rgb565(bg, fg, a4));
        }
    }
}
