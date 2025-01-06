#include "ui.h"
#include <stddef.h>
#include <stdint.h>

#include "src/core/math_util.h"
#include "src/core/trace_format.h"
#include "util/crc32.h"
#include "ui_display.h"
#include "ui_font_stroke.h"
#include "ui_color.h"
#ifdef UI_PIXEL_SIM
#include "ui_pixel_sink.h"
#endif

#ifndef UI_LCD_HW
#ifdef HOST_TEST
#define UI_LCD_HW 0
#else
#define UI_LCD_HW 1
#endif
#endif

#if UI_LCD_HW
#include "ui_lcd.h"
#endif
#define G 8u
#define PAD (2u * G)
#define TOP_Y 8u
#define TOP_H (3u * G)
#define SPEED_X PAD
#define SPEED_Y (TOP_Y + TOP_H + G)
#define SPEED_W (DISP_W - 2u * PAD)
#define SPEED_H 96u
#define ASSIST_Y (SPEED_Y + SPEED_H + G)
#define ASSIST_H 28u
#define STATS_Y (ASSIST_Y + ASSIST_H + G)
#define STATS_H 32u
#define STATS_GAP 8u
#define STATS_W ((DISP_W - 2u * PAD - STATS_GAP) / 2u)
#define STATS_LX PAD
#define STATS_RX (PAD + STATS_W + STATS_GAP)
#define STATS_Y2 (STATS_Y + STATS_H + G)
#define CARDS_Y (DISP_H - PAD - 48u)
#define CARDS_H 48u
#define GRAPH_H 140u
#define GRAPH_Y (TOP_Y + TOP_H + G + 8u)
#define GRAPH_W (DISP_W - 2u * PAD)
#define GRAPH_X PAD
#define UI_PANEL_DITHER_LEVEL 4u
#define UI_PANEL_DITHER_MIN_AREA 8000u
#define UI_PANEL_DITHER_TINT 24u

#define MAX_DIRTY UI_MAX_DIRTY

#define MM_PER_MILE 1609340u
#define MM_PER_KM 1000000u

#define LIMIT_REASON_USER 0u
#define LIMIT_REASON_LUG 1u
#define LIMIT_REASON_THERM 2u
#define LIMIT_REASON_SAG 3u
#define ICON_SIZE 16u

typedef enum {
    UI_ICON_NONE = 0,
    UI_ICON_BLE,
    UI_ICON_LOCK,
    UI_ICON_THERMO,
    UI_ICON_GRAPH,
    UI_ICON_TRIP,
    UI_ICON_SETTINGS,
    UI_ICON_CRUISE,
    UI_ICON_BATTERY,
    UI_ICON_ALERT,
    UI_ICON_BUS,
    UI_ICON_CAPTURE,
    UI_ICON_TUNE,
    UI_ICON_INFO,
    UI_ICON_PROFILE
} ui_icon_id_t;

struct ui_dirty {
    ui_rect_t rects[MAX_DIRTY];
    uint8_t count;
    uint8_t full;
};

struct ui_render_ctx {
    ui_state_t *ui;
    const ui_palette_t *palette;
    uint8_t hash_enabled;
    uint8_t count_ops;
    uint8_t draw_enabled;
};

static void render_header(ui_render_ctx_t *ctx, const char *title);
static void render_header_icon(ui_render_ctx_t *ctx, const char *title, ui_icon_id_t icon);
static void render_table_header(ui_render_ctx_t *ctx, uint16_t y, const char *left, const char *right);
static void render_table_row(ui_render_ctx_t *ctx, uint16_t y, const char *label, int32_t value);
static void render_table_row_text(ui_render_ctx_t *ctx, uint16_t y, const char *label, const char *value);
static void render_table_row_hex(ui_render_ctx_t *ctx, uint16_t y, const char *label, uint32_t value);
static uint16_t rgb565_lerp(uint16_t a, uint16_t b, uint8_t t);

static const ui_palette_t k_ui_palettes[UI_THEME_COUNT] = {
    /* UI_THEME_DAY */
    {
        {
            0xFFFFu, /* bg */
            0xE73Cu, /* panel */
            0x0000u, /* text */
            0x7BEFu, /* muted */
            0x219Fu, /* accent */
            0xFFE0u, /* warn */
            0xF800u, /* danger */
            0x07E0u  /* ok */
        }
    },
    /* UI_THEME_NIGHT */
    {
        {
            0x0000u, /* bg */
            0x10C4u, /* panel */
            0xFFFFu, /* text */
            0x7BEFu, /* muted */
            0x07FFu, /* accent */
            0xFD20u, /* warn */
            0xF800u, /* danger */
            0x07E0u  /* ok */
        }
    },
    /* UI_THEME_HIGH_CONTRAST */
    {
        {
            0x0000u, /* bg */
            0xFFFFu, /* panel */
            0x0000u, /* text */
            0xFFFFu, /* muted */
            0xFFE0u, /* accent */
            0xFFE0u, /* warn */
            0xF800u, /* danger */
            0x07E0u  /* ok */
        }
    },
    /* UI_THEME_COLORBLIND */
    {
        {
            0xFFFFu, /* bg */
            0xDEFBu, /* panel */
            0x0000u, /* text */
            0x7BEFu, /* muted */
            0x001Fu, /* accent */
            0xFD20u, /* warn */
            0xF81Fu, /* danger */
            0x07FFu  /* ok */
        }
    }
};

static uint8_t theme_normalize(uint8_t theme_id)
{
    if (theme_id >= UI_THEME_COUNT)
        return UI_THEME_DAY;
    return theme_id;
}

static const char *theme_name(uint8_t theme_id)
{
    switch (theme_normalize(theme_id))
    {
    case UI_THEME_DAY:
        return "DAY";
    case UI_THEME_NIGHT:
        return "NIGHT";
    case UI_THEME_HIGH_CONTRAST:
        return "HI-CON";
    case UI_THEME_COLORBLIND:
        return "CBLIND";
    default:
        return "DAY";
    }
}

uint8_t ui_theme_normalize(uint8_t theme_id)
{
    return theme_normalize(theme_id);
}

const ui_palette_t *ui_theme_palette(uint8_t theme_id)
{
    return &k_ui_palettes[theme_normalize(theme_id)];
}

static uint8_t panel_flags_for_theme(uint8_t theme_id)
{
    switch (theme_normalize(theme_id))
    {
    case UI_THEME_DAY:
    case UI_THEME_COLORBLIND:
        return UI_PANEL_FLAG_DITHER;
    default:
        return 0u;
    }
}

static uint16_t ui_color(const ui_render_ctx_t *ctx, ui_color_id_t id)
{
    if (!ctx || !ctx->palette)
        return 0;
    if ((uint8_t)id >= UI_PALETTE_COLORS)
        return ctx->palette->colors[0];
    return ctx->palette->colors[id];
}

static void hash_u32(ui_render_ctx_t *ctx, uint32_t v)
{
    if (!ctx->hash_enabled)
        return;
    uint8_t buf[4];
    buf[0] = (uint8_t)(v & 0xFFu);
    buf[1] = (uint8_t)((v >> 8) & 0xFFu);
    buf[2] = (uint8_t)((v >> 16) & 0xFFu);
    buf[3] = (uint8_t)((v >> 24) & 0xFFu);
    ctx->ui->hash = crc32_update(ctx->ui->hash, buf, sizeof(buf));
}

static size_t ui_strlen(const char *s)
{
    size_t n = 0;
    if (!s)
        return 0;
    while (s[n] != '\0')
        n++;
    return n;
}

static void hash_bytes(ui_render_ctx_t *ctx, const char *s)
{
    if (!ctx->hash_enabled || !s)
        return;
    ctx->ui->hash = crc32_update(ctx->ui->hash, (const uint8_t *)s, ui_strlen(s));
}

static void draw_op(ui_render_ctx_t *ctx, uint32_t op_id)
{
    hash_u32(ctx, op_id);
    if (ctx->count_ops)
        ctx->ui->draw_ops++;
}

void ui_draw_round_rect(ui_render_ctx_t *ctx, ui_rect_t r, uint16_t color, uint8_t radius)
{
    draw_op(ctx, 1u);
    hash_u32(ctx, r.x);
    hash_u32(ctx, r.y);
    hash_u32(ctx, r.w);
    hash_u32(ctx, r.h);
    hash_u32(ctx, color);
    hash_u32(ctx, radius);
    if (!ctx->draw_enabled)
        return;
#ifdef UI_PIXEL_SIM
    ui_pixel_sink_draw_round_rect(r.x, r.y, r.w, r.h, color, radius);
#elif UI_LCD_HW
    ui_lcd_fill_round_rect(r.x, r.y, r.w, r.h, color, radius);
#endif
}

void ui_draw_rect(ui_render_ctx_t *ctx, ui_rect_t r, uint16_t color)
{
    draw_op(ctx, 2u);
    hash_u32(ctx, r.x);
    hash_u32(ctx, r.y);
    hash_u32(ctx, r.w);
    hash_u32(ctx, r.h);
    hash_u32(ctx, color);
    if (!ctx->draw_enabled)
        return;
#ifdef UI_PIXEL_SIM
    ui_pixel_sink_draw_rect(r.x, r.y, r.w, r.h, color);
#elif UI_LCD_HW
    ui_lcd_fill_rect(r.x, r.y, r.w, r.h, color);
#endif
}

static void ui_draw_round_rect_dither(ui_render_ctx_t *ctx, ui_rect_t r, uint16_t color, uint16_t alt,
                                      uint8_t radius, uint8_t level)
{
    draw_op(ctx, 3u);
    hash_u32(ctx, r.x);
    hash_u32(ctx, r.y);
    hash_u32(ctx, r.w);
    hash_u32(ctx, r.h);
    hash_u32(ctx, color);
    hash_u32(ctx, alt);
    hash_u32(ctx, radius);
    hash_u32(ctx, level);
    if (!ctx->draw_enabled)
        return;
#ifdef UI_PIXEL_SIM
    ui_pixel_sink_draw_round_rect_dither(r.x, r.y, r.w, r.h, color, alt, radius, level);
#elif UI_LCD_HW
    ui_lcd_fill_round_rect_dither(r.x, r.y, r.w, r.h, color, alt, radius, level);
#endif
}

void ui_draw_text(ui_render_ctx_t *ctx, uint16_t x, uint16_t y, const char *text, uint16_t fg, uint16_t bg)
{
    draw_op(ctx, 4u);
    hash_u32(ctx, x);
    hash_u32(ctx, y);
    hash_u32(ctx, fg);
    hash_u32(ctx, bg);
    hash_bytes(ctx, text);
    if (!ctx->draw_enabled)
        return;
#ifdef UI_PIXEL_SIM
    ui_pixel_sink_draw_text(x, y, text, fg, bg);
#elif UI_LCD_HW
    ui_lcd_draw_text_stroke(x, y, text, fg, bg);
#endif
}

void ui_draw_value(ui_render_ctx_t *ctx, uint16_t x, uint16_t y, const char *label, int32_t value, uint16_t fg, uint16_t bg)
{
    draw_op(ctx, 5u);
    hash_u32(ctx, x);
    hash_u32(ctx, y);
    hash_bytes(ctx, label);
    hash_u32(ctx, (uint32_t)value);
    hash_u32(ctx, fg);
    hash_u32(ctx, bg);
    if (!ctx->draw_enabled)
        return;
#ifdef UI_PIXEL_SIM
    ui_pixel_sink_draw_value(x, y, label, value, fg, bg);
#elif UI_LCD_HW
    ui_lcd_draw_value_stroke(x, y, label, value, fg, bg);
#endif
}

void ui_draw_big_digit(ui_render_ctx_t *ctx, uint16_t x, uint16_t y, uint8_t digit, uint8_t scale, uint16_t color)
{
    static const uint8_t segs[10] = {
        0x3Fu, /* 0 */
        0x06u, /* 1 */
        0x5Bu, /* 2 */
        0x4Fu, /* 3 */
        0x66u, /* 4 */
        0x6Du, /* 5 */
        0x7Du, /* 6 */
        0x07u, /* 7 */
        0x7Fu, /* 8 */
        0x6Fu  /* 9 */
    };
    draw_op(ctx, 6u);
    hash_u32(ctx, x);
    hash_u32(ctx, y);
    hash_u32(ctx, digit);
    hash_u32(ctx, scale);
    if (digit < 10)
        hash_u32(ctx, segs[digit]);
    hash_u32(ctx, color);
    if (!ctx->draw_enabled)
        return;
#ifdef UI_PIXEL_SIM
    ui_pixel_sink_draw_big_digit(x, y, digit, scale, color);
#elif UI_LCD_HW
    ui_lcd_draw_big_digit_7seg(x, y, digit, scale, color);
#endif
}

void ui_draw_battery_icon(ui_render_ctx_t *ctx, ui_rect_t r, uint8_t soc, uint16_t color, uint16_t bg)
{
    draw_op(ctx, 7u);
    hash_u32(ctx, r.x);
    hash_u32(ctx, r.y);
    hash_u32(ctx, r.w);
    hash_u32(ctx, r.h);
    hash_u32(ctx, soc);
    hash_u32(ctx, color);
    hash_u32(ctx, bg);
    if (!ctx->draw_enabled)
        return;
#ifdef UI_PIXEL_SIM
    ui_pixel_sink_draw_battery_icon(r.x, r.y, r.w, r.h, soc, color, bg);
#elif UI_LCD_HW
    ui_lcd_draw_battery_icon(r.x, r.y, r.w, r.h, soc, color, bg);
#endif
}

void ui_draw_warning_icon(ui_render_ctx_t *ctx, uint16_t x, uint16_t y, uint16_t color)
{
    draw_op(ctx, 8u);
    hash_u32(ctx, x);
    hash_u32(ctx, y);
    hash_u32(ctx, color);
    if (!ctx->draw_enabled)
        return;
#ifdef UI_PIXEL_SIM
    ui_pixel_sink_draw_warning_icon(x, y, color);
#elif UI_LCD_HW
    ui_lcd_draw_warning_icon(x, y, color);
#endif
}

void ui_draw_ring_arc(ui_render_ctx_t *ctx, ui_rect_t clip,
                      int16_t cx, int16_t cy, uint16_t outer_r, uint16_t thickness,
                      int16_t start_deg_cw, uint16_t sweep_deg_cw,
                      uint16_t fg, uint16_t bg)
{
    draw_op(ctx, 10u);
    hash_u32(ctx, clip.x);
    hash_u32(ctx, clip.y);
    hash_u32(ctx, clip.w);
    hash_u32(ctx, clip.h);
    hash_u32(ctx, (uint32_t)(int32_t)cx);
    hash_u32(ctx, (uint32_t)(int32_t)cy);
    hash_u32(ctx, outer_r);
    hash_u32(ctx, thickness);
    hash_u32(ctx, (uint32_t)(int32_t)start_deg_cw);
    hash_u32(ctx, sweep_deg_cw);
    hash_u32(ctx, fg);
    hash_u32(ctx, bg);
    if (!ctx->draw_enabled)
        return;
#ifdef UI_PIXEL_SIM
    ui_pixel_sink_draw_ring_arc_a4(clip.x, clip.y, clip.w, clip.h,
                                   cx, cy, outer_r, thickness,
                                   start_deg_cw, sweep_deg_cw,
                                   fg, bg);
#elif UI_LCD_HW
    ui_lcd_draw_ring_arc_a4(clip.x, clip.y, clip.w, clip.h,
                            cx, cy, outer_r, thickness,
                            start_deg_cw, sweep_deg_cw,
                            fg, bg);
#endif
}

void ui_draw_ring_gauge(ui_render_ctx_t *ctx, ui_rect_t clip,
                        int16_t cx, int16_t cy, uint16_t outer_r, uint16_t thickness,
                        int16_t start_deg_cw, uint16_t sweep_deg_cw, uint16_t active_sweep_deg_cw,
                        uint16_t fg_active, uint16_t fg_inactive, uint16_t bg)
{
    draw_op(ctx, 11u);
    hash_u32(ctx, clip.x);
    hash_u32(ctx, clip.y);
    hash_u32(ctx, clip.w);
    hash_u32(ctx, clip.h);
    hash_u32(ctx, (uint32_t)(int32_t)cx);
    hash_u32(ctx, (uint32_t)(int32_t)cy);
    hash_u32(ctx, outer_r);
    hash_u32(ctx, thickness);
    hash_u32(ctx, (uint32_t)(int32_t)start_deg_cw);
    hash_u32(ctx, sweep_deg_cw);
    hash_u32(ctx, active_sweep_deg_cw);
    hash_u32(ctx, fg_active);
    hash_u32(ctx, fg_inactive);
    hash_u32(ctx, bg);
    if (!ctx->draw_enabled)
        return;
#ifdef UI_PIXEL_SIM
    ui_pixel_sink_draw_ring_gauge_a4(clip.x, clip.y, clip.w, clip.h,
                                     cx, cy, outer_r, thickness,
                                     start_deg_cw, sweep_deg_cw, active_sweep_deg_cw,
                                     fg_active, fg_inactive, bg);
#elif UI_LCD_HW
    ui_lcd_draw_ring_gauge_a4(clip.x, clip.y, clip.w, clip.h,
                              cx, cy, outer_r, thickness,
                              start_deg_cw, sweep_deg_cw, active_sweep_deg_cw,
                              fg_active, fg_inactive, bg);
#endif
}

#if defined(UI_PIXEL_SIM) || UI_LCD_HW
static uint16_t icon_min_u16(uint16_t a, uint16_t b)
{
    return (a < b) ? a : b;
}

static void icon_rect(ui_render_ctx_t *ctx, uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color)
{
    if (w == 0u || h == 0u)
        return;
    ui_draw_rect(ctx, (ui_rect_t){x, y, w, h}, color);
}

static void icon_round(ui_render_ctx_t *ctx, uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color)
{
    uint16_t min_dim = icon_min_u16(w, h);
    uint8_t radius = (uint8_t)(min_dim / 2u);
    if (w == 0u || h == 0u)
        return;
    ui_draw_round_rect(ctx, (ui_rect_t){x, y, w, h}, color, radius);
}

static void icon_ring(ui_render_ctx_t *ctx, uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                      uint16_t thick, uint16_t fg, uint16_t bg)
{
    uint16_t min_dim = icon_min_u16(w, h);
    uint8_t radius = (uint8_t)(min_dim / 2u);
    if (w == 0u || h == 0u)
        return;
    ui_draw_round_rect(ctx, (ui_rect_t){x, y, w, h}, fg, radius);
    if (w > 2u * thick && h > 2u * thick)
        ui_draw_round_rect(ctx, (ui_rect_t){(uint16_t)(x + thick), (uint16_t)(y + thick),
                                            (uint16_t)(w - 2u * thick), (uint16_t)(h - 2u * thick)},
                           bg, (uint8_t)(radius > thick ? radius - thick : 1u));
}

static void ui_draw_icon(ui_render_ctx_t *ctx, uint16_t x, uint16_t y, ui_icon_id_t icon,
                         uint16_t fg, uint16_t bg)
{
    const uint16_t s = ICON_SIZE;
    const uint16_t t = 2u;
    switch (icon)
    {
    case UI_ICON_BLE:
        icon_rect(ctx, (uint16_t)(x + 3u), (uint16_t)(y + 3u), 2u, (uint16_t)(s - 6u), fg);
        icon_ring(ctx, (uint16_t)(x + 4u), (uint16_t)(y + 2u), 9u, 6u, t, fg, bg);
        icon_ring(ctx, (uint16_t)(x + 4u), (uint16_t)(y + 8u), 9u, 6u, t, fg, bg);
        break;
    case UI_ICON_LOCK:
        icon_ring(ctx, (uint16_t)(x + 3u), (uint16_t)(y + 1u), 10u, 8u, t, fg, bg);
        icon_round(ctx, (uint16_t)(x + 3u), (uint16_t)(y + 8u), 10u, 7u, fg);
        icon_rect(ctx, (uint16_t)(x + 7u), (uint16_t)(y + 11u), 2u, 3u, bg);
        break;
    case UI_ICON_THERMO:
        icon_rect(ctx, (uint16_t)(x + 7u), (uint16_t)(y + 3u), 2u, 7u, fg);
        icon_round(ctx, (uint16_t)(x + 5u), (uint16_t)(y + 9u), 6u, 6u, fg);
        icon_round(ctx, (uint16_t)(x + 6u), (uint16_t)(y + 10u), 4u, 4u, bg);
        break;
    case UI_ICON_GRAPH:
        icon_rect(ctx, (uint16_t)(x + 2u), (uint16_t)(y + 8u), 3u, 6u, fg);
        icon_rect(ctx, (uint16_t)(x + 7u), (uint16_t)(y + 5u), 3u, 9u, fg);
        icon_rect(ctx, (uint16_t)(x + 12u), (uint16_t)(y + 10u), 3u, 4u, fg);
        break;
    case UI_ICON_TRIP:
        icon_rect(ctx, (uint16_t)(x + 3u), (uint16_t)(y + 2u), 2u, 12u, fg);
        icon_rect(ctx, (uint16_t)(x + 5u), (uint16_t)(y + 2u), 7u, 5u, fg);
        icon_rect(ctx, (uint16_t)(x + 3u), (uint16_t)(y + 13u), 9u, 2u, fg);
        break;
    case UI_ICON_SETTINGS:
        icon_ring(ctx, (uint16_t)(x + 3u), (uint16_t)(y + 3u), 10u, 10u, t, fg, bg);
        icon_rect(ctx, (uint16_t)(x + 7u), (uint16_t)(y + 1u), 2u, 3u, fg);
        icon_rect(ctx, (uint16_t)(x + 7u), (uint16_t)(y + 12u), 2u, 3u, fg);
        icon_rect(ctx, (uint16_t)(x + 1u), (uint16_t)(y + 7u), 3u, 2u, fg);
        icon_rect(ctx, (uint16_t)(x + 12u), (uint16_t)(y + 7u), 3u, 2u, fg);
        break;
    case UI_ICON_CRUISE:
        icon_ring(ctx, (uint16_t)(x + 3u), (uint16_t)(y + 3u), 10u, 10u, t, fg, bg);
        icon_rect(ctx, (uint16_t)(x + 8u), (uint16_t)(y + 6u), 2u, 5u, fg);
        break;
    case UI_ICON_BATTERY:
        ui_draw_battery_icon(ctx, (ui_rect_t){(uint16_t)(x + 1u), (uint16_t)(y + 5u), 14u, 6u}, 100u, fg, bg);
        break;
    case UI_ICON_ALERT:
        ui_draw_warning_icon(ctx, (uint16_t)(x + 2u), (uint16_t)(y + 2u), fg);
        break;
    case UI_ICON_BUS:
        icon_round(ctx, (uint16_t)(x + 2u), (uint16_t)(y + 3u), 12u, 4u, fg);
        icon_round(ctx, (uint16_t)(x + 2u), (uint16_t)(y + 9u), 12u, 4u, fg);
        icon_rect(ctx, (uint16_t)(x + 4u), (uint16_t)(y + 5u), 2u, 2u, bg);
        icon_rect(ctx, (uint16_t)(x + 4u), (uint16_t)(y + 11u), 2u, 2u, bg);
        break;
    case UI_ICON_CAPTURE:
        icon_ring(ctx, (uint16_t)(x + 3u), (uint16_t)(y + 3u), 10u, 10u, t, fg, bg);
        icon_round(ctx, (uint16_t)(x + 7u), (uint16_t)(y + 7u), 2u, 2u, fg);
        break;
    case UI_ICON_TUNE:
        icon_rect(ctx, (uint16_t)(x + 4u), (uint16_t)(y + 3u), 2u, 10u, fg);
        icon_rect(ctx, (uint16_t)(x + 7u), (uint16_t)(y + 4u), 2u, 9u, fg);
        icon_rect(ctx, (uint16_t)(x + 10u), (uint16_t)(y + 2u), 2u, 12u, fg);
        icon_rect(ctx, (uint16_t)(x + 3u), (uint16_t)(y + 7u), 4u, 2u, fg);
        icon_rect(ctx, (uint16_t)(x + 6u), (uint16_t)(y + 9u), 4u, 2u, fg);
        icon_rect(ctx, (uint16_t)(x + 9u), (uint16_t)(y + 5u), 4u, 2u, fg);
        break;
    case UI_ICON_INFO:
        icon_ring(ctx, (uint16_t)(x + 2u), (uint16_t)(y + 2u), 12u, 12u, t, fg, bg);
        icon_rect(ctx, (uint16_t)(x + 7u), (uint16_t)(y + 6u), 2u, 5u, fg);
        icon_rect(ctx, (uint16_t)(x + 7u), (uint16_t)(y + 4u), 2u, 2u, fg);
        break;
    case UI_ICON_PROFILE:
        icon_round(ctx, (uint16_t)(x + 5u), (uint16_t)(y + 2u), 6u, 6u, fg);
        icon_round(ctx, (uint16_t)(x + 3u), (uint16_t)(y + 8u), 10u, 6u, fg);
        break;
    default:
        break;
    }
}
#endif

void ui_dirty_add(ui_dirty_t *d, ui_rect_t r)
{
    if (d->full)
        return;
    if (d->count >= MAX_DIRTY)
    {
        d->full = 1u;
        d->count = 1u;
        d->rects[0] = (ui_rect_t){0, 0, DISP_W, DISP_H};
        return;
    }
    d->rects[d->count++] = r;
}

void ui_dirty_full(ui_dirty_t *d)
{
    if (!d)
        return;
    d->full = 1u;
    d->count = 1u;
    d->rects[0] = (ui_rect_t){0, 0, DISP_W, DISP_H};
}

static uint8_t rect_intersects(ui_rect_t a, ui_rect_t b)
{
    uint16_t ax1 = a.x + a.w;
    uint16_t ay1 = a.y + a.h;
    uint16_t bx1 = b.x + b.w;
    uint16_t by1 = b.y + b.h;
    return (a.x < bx1 && ax1 > b.x && a.y < by1 && ay1 > b.y) ? 1u : 0u;
}

static uint8_t rect_dirty(const ui_dirty_t *d, ui_rect_t r)
{
    if (d->full)
        return 1u;
    for (uint8_t i = 0; i < d->count; ++i)
        if (rect_intersects(d->rects[i], r))
            return 1u;
    return 0u;
}

static uint16_t trip_distance_d10(const ui_model_t *m)
{
    uint32_t unit_mm = (m->units ? MM_PER_KM : MM_PER_MILE);
    if (unit_mm == 0 || m->trip_distance_mm == 0)
        return 0;
    uint64_t num = (uint64_t)m->trip_distance_mm * 10ull + (unit_mm / 2u);
    uint32_t val = divu64_32(num, unit_mm);
    return (val > 0xFFFFu) ? 0xFFFFu : (uint16_t)val;
}

static uint16_t trip_wh_per_unit_d10(const ui_model_t *m)
{
    if (m->trip_distance_mm == 0 || m->trip_energy_mwh == 0)
        return 0;
    uint32_t unit_mm = (m->units ? MM_PER_KM : MM_PER_MILE);
    uint64_t num = (uint64_t)m->trip_energy_mwh * 10ull * (uint64_t)unit_mm;
    uint32_t den = m->trip_distance_mm * 1000u;
    if (den == 0)
        return 0;
    uint32_t val = divu64_32(num + (den / 2u), den);
    return (val > 0xFFFFu) ? 0xFFFFu : (uint16_t)val;
}

static void ui_graph_sample(ui_state_t *ui, const ui_model_t *m)
{
    if (!ui || !m)
        return;
    if (ui->graph_channel != m->graph_channel)
    {
        ui->graph_channel = m->graph_channel;
        ui->graph_head = 0u;
        ui->graph_count = 0u;
    }
    uint16_t sample = 0u;
    switch (m->graph_channel)
    {
    case UI_GRAPH_CH_POWER:
        sample = m->power_w;
        break;
    case UI_GRAPH_CH_VOLT:
        sample = (uint16_t)((m->batt_dV > 0) ? m->batt_dV : 0);
        break;
    case UI_GRAPH_CH_CAD:
        sample = m->cadence_rpm;
        break;
    case UI_GRAPH_CH_SPEED:
    default:
        sample = (uint16_t)(m->speed_dmph / 10u);
        break;
    }
    if (UI_GRAPH_SAMPLES == 0u)
        return;
    ui->graph_samples[ui->graph_head] = sample;
    ui->graph_head = (uint8_t)((ui->graph_head + 1u) % UI_GRAPH_SAMPLES);
    if (ui->graph_count < UI_GRAPH_SAMPLES)
        ui->graph_count++;
}

static uint16_t seg_digit_w(uint8_t scale)
{
    /* Must match the pixel sink's 7-seg renderer (host + eventual target). */
    return (uint16_t)(12u * (uint16_t)scale);
}

static void fmt_u32(char *out, size_t len, uint32_t v)
{
    if (!out || len == 0)
        return;
    char *ptr = out;
    size_t rem = len - 1u;
    append_u32(&ptr, &rem, v);
    *ptr = 0;
}

static void fmt_u32_pad4(char *out, size_t len, uint32_t v)
{
    if (!out || len == 0)
        return;
    if (len < 5)
    {
        out[0] = 0;
        return;
    }
    v %= 10000u;
    char tmp[12];
    char *ptr = tmp;
    size_t rem = sizeof(tmp) - 1u;
    append_u32(&ptr, &rem, v);
    *ptr = 0;
    size_t digits = (size_t)(ptr - tmp);
    size_t pad = (digits < 4u) ? (4u - digits) : 0u;
    size_t i = 0;
    for (; i < pad && i + 1 < len; ++i)
        out[i] = '0';
    for (size_t j = 0; tmp[j] && i + 1 < len; ++j)
        out[i++] = tmp[j];
    out[i] = 0;
}

static void fmt_u32_hex8(char *out, size_t len, uint32_t v)
{
    if (!out || len == 0)
        return;
    char *ptr = out;
    size_t rem = len - 1u;
    append_hex_u32(&ptr, &rem, v);
    *ptr = 0;
    for (size_t i = 0; out[i]; ++i)
    {
        char c = out[i];
        if (c >= 'a' && c <= 'f')
            out[i] = (char)(c - ('a' - 'A'));
    }
}

static void fmt_d10(char *out, size_t len, int32_t v_d10)
{
    if (!out || len == 0)
        return;
    char *ptr = out;
    size_t rem = len - 1u;
    if (v_d10 < 0)
    {
        append_char(&ptr, &rem, '-');
        v_d10 = -v_d10;
    }
    uint32_t ip = (uint32_t)v_d10 / 10u;
    uint32_t fp = (uint32_t)v_d10 % 10u;
    append_u32(&ptr, &rem, ip);
    if (rem >= 2u)
    {
        append_char(&ptr, &rem, '.');
        append_char(&ptr, &rem, (char)('0' + (char)fp));
    }
    *ptr = 0;
}

static void fmt_seconds_label(char *out, size_t len, uint32_t seconds)
{
    if (!out || len == 0)
        return;
    uint32_t mins = seconds / 60u;
    uint32_t secs = seconds % 60u;
    if (mins > 99u)
        mins = 99u;
    size_t i = 0;
    if (mins > 0u)
    {
        char buf[6];
        fmt_u32(buf, sizeof(buf), mins);
        for (size_t j = 0; buf[j] && i + 1 < len; ++j)
            out[i++] = buf[j];
        if (i + 1 < len)
            out[i++] = 'm';
        if (i + 1 < len)
            out[i++] = (char)('0' + (secs / 10u));
        if (i + 1 < len)
            out[i++] = (char)('0' + (secs % 10u));
        if (i + 1 < len)
            out[i++] = 's';
    }
    else
    {
        char buf[6];
        fmt_u32(buf, sizeof(buf), secs);
        for (size_t j = 0; buf[j] && i + 1 < len; ++j)
            out[i++] = buf[j];
        if (i + 1 < len)
            out[i++] = 's';
    }
    out[i < len ? i : (len - 1u)] = 0;
}

static void fmt_distance_label(char *out, size_t len, uint16_t dist_d10, uint8_t units_metric)
{
    if (!out || len == 0)
        return;
    uint16_t dist = dist_d10;
    if (dist > 999u)
        dist = 999u;
    char num[12];
    fmt_d10(num, sizeof(num), (int32_t)dist);
    size_t i = 0;
    for (size_t j = 0; num[j] && i + 1 < len; ++j)
        out[i++] = num[j];
    const char *units = units_metric ? "km" : "mi";
    for (size_t j = 0; units[j] && i + 1 < len; ++j)
        out[i++] = units[j];
    out[i < len ? i : (len - 1u)] = 0;
}

static void fmt_time_hhmm(char *out, size_t len, uint32_t ms)
{
    if (!out || len == 0)
        return;
    uint32_t total_sec = ms / 1000u;
    uint32_t hours = total_sec / 3600u;
    uint32_t minutes = (total_sec / 60u) % 60u;
    char hbuf[6];
    fmt_u32(hbuf, sizeof(hbuf), hours);
    size_t i = 0;
    while (hbuf[i] && i + 1 < len)
    {
        out[i] = hbuf[i];
        i++;
    }
    if (i + 1 < len)
        out[i++] = ':';
    if (i + 1 < len)
        out[i++] = (char)('0' + (minutes / 10u));
    if (i + 1 < len)
        out[i++] = (char)('0' + (minutes % 10u));
    out[i < len ? i : (len - 1u)] = 0;
}

static const char *alert_type_label(uint8_t type)
{
    switch (type)
    {
        case 1u: return "BRAKE";
        case 2u: return "COMM";
        case 3u: return "DROP";
        case 4u: return "TEMP";
        case 5u: return "DERATE";
        case 6u: return "CRUISE";
        case 7u: return "CFG";
        case 8u: return "PIN";
        case 9u: return "RESET";
        case 10u: return "BUS";
        default: return "EVENT";
    }
}

#if defined(UI_PIXEL_SIM) || UI_LCD_HW
static ui_icon_id_t alert_type_icon(uint8_t type)
{
    switch (type)
    {
        case 2u: return UI_ICON_BLE;
        case 4u: return UI_ICON_THERMO;
        case 5u: return UI_ICON_THERMO;
        case 6u: return UI_ICON_CRUISE;
        case 7u: return UI_ICON_SETTINGS;
        case 8u: return UI_ICON_LOCK;
        case 9u: return UI_ICON_INFO;
        case 10u: return UI_ICON_BUS;
        default: return UI_ICON_ALERT;
    }
}
#endif

static ui_rect_t inset_rect(ui_rect_t r, uint16_t d)
{
    ui_rect_t o = r;
    if (o.w > 2u * d)
    {
        o.x = (uint16_t)(o.x + d);
        o.w = (uint16_t)(o.w - 2u * d);
    }
    if (o.h > 2u * d)
    {
        o.y = (uint16_t)(o.y + d);
        o.h = (uint16_t)(o.h - 2u * d);
    }
    return o;
}

static uint8_t panel_dither_enabled(const ui_render_ctx_t *ctx, const ui_panel_style_t *style, ui_rect_t r)
{
    if (!ctx || !style)
        return 0u;
    if ((style->flags & UI_PANEL_FLAG_DITHER) == 0u)
        return 0u;
    if ((uint32_t)r.w * (uint32_t)r.h < UI_PANEL_DITHER_MIN_AREA)
        return 0u;
    return 1u;
}

static void ui_draw_drop_shadow(ui_render_ctx_t *ctx, ui_rect_t r, uint8_t radius,
                                int8_t dx, int8_t dy, uint16_t color)
{
    if ((dx == 0) && (dy == 0))
        return;
    int sx = (int)r.x + (int)dx;
    int sy = (int)r.y + (int)dy;
    if (sx < 0)
        sx = 0;
    if (sy < 0)
        sy = 0;
    ui_draw_round_rect(ctx, (ui_rect_t){(uint16_t)sx, (uint16_t)sy, r.w, r.h}, color, radius);
}

void ui_draw_panel(ui_render_ctx_t *ctx, ui_rect_t r, const ui_panel_style_t *style)
{
    if (!style)
    {
        ui_draw_round_rect(ctx, r, ui_color(ctx, UI_COLOR_PANEL), 8u);
        return;
    }

    uint8_t rad = style->radius;
    uint8_t bt = style->border_thick;
    uint8_t dither = panel_dither_enabled(ctx, style, r);
    uint16_t dither_alt = style->fill;
    if (dither)
        dither_alt = rgb565_lerp(style->fill, ui_color(ctx, UI_COLOR_BG), UI_PANEL_DITHER_TINT);

    if (style->shadow && (style->shadow_dx || style->shadow_dy))
    {
        ui_draw_drop_shadow(ctx, r, rad, style->shadow_dx, style->shadow_dy, style->shadow);
    }

    if (bt == 0u)
    {
        if (dither)
            ui_draw_round_rect_dither(ctx, r, style->fill, dither_alt, rad, UI_PANEL_DITHER_LEVEL);
        else
            ui_draw_round_rect(ctx, r, style->fill, rad);
        return;
    }

    ui_draw_round_rect(ctx, r, style->border, rad);
    ui_rect_t inner = inset_rect(r, bt);
    if (inner.w >= 2u && inner.h >= 2u)
    {
        uint8_t inner_rad = rad;
        if (inner_rad > bt)
            inner_rad = (uint8_t)(inner_rad - bt);
        else
            inner_rad = 1u;
        if (dither)
            ui_draw_round_rect_dither(ctx, inner, style->fill, dither_alt, inner_rad, UI_PANEL_DITHER_LEVEL);
        else
            ui_draw_round_rect(ctx, inner, style->fill, inner_rad);
    }
}

static void draw_outline_panel(ui_render_ctx_t *ctx, ui_rect_t r, uint16_t border, uint16_t fill, uint8_t radius)
{
    const uint16_t t = 2u;
    ui_draw_round_rect(ctx, r, border, radius);
    ui_rect_t inner = inset_rect(r, t);
    if (inner.w >= 4u && inner.h >= 4u)
        ui_draw_round_rect(ctx, inner, fill, (uint8_t)(radius > 2u ? radius - 2u : 1u));
}

static uint16_t txt_w_est(const char *s)
{
    return ui_font_stroke_text_width_px(s);
}

static uint16_t rgb565_lerp(uint16_t a, uint16_t b, uint8_t t)
{
    /* Linear interpolation in RGB565 space (t in [0..255]). */
    uint8_t ar = (a >> 11) & 0x1Fu;
    uint8_t ag = (a >> 5) & 0x3Fu;
    uint8_t ab = a & 0x1Fu;
    uint8_t br = (b >> 11) & 0x1Fu;
    uint8_t bg = (b >> 5) & 0x3Fu;
    uint8_t bb = b & 0x1Fu;

    uint16_t inv = (uint16_t)(255u - t);
    uint8_t r = (uint8_t)((ar * inv + br * t + 127u) / 255u);
    uint8_t g = (uint8_t)((ag * inv + bg * t + 127u) / 255u);
    uint8_t bl = (uint8_t)((ab * inv + bb * t + 127u) / 255u);
    return (uint16_t)((r << 11) | (g << 5) | bl);
}

typedef struct {
    uint16_t U;
    uint16_t M;
    uint16_t GAP;
    uint16_t ST;
    uint8_t R;
    ui_rect_t full;
    ui_rect_t top_area;
    ui_rect_t speed;
    ui_rect_t speed_in;
    ui_rect_t tray;
    ui_rect_t tray_in;
} ui_dash_v2_layout_t;

static ui_dash_v2_layout_t dash_v2_layout(void)
{
    ui_dash_v2_layout_t l = {0};
    l.U = 4u;
    l.M = 12u;
    l.GAP = 10u;
    l.ST = 2u;
    l.R = 16u;
    l.full = (ui_rect_t){0, 0, DISP_W, DISP_H};

    const uint16_t top_y = l.M;
    const uint16_t top_h = 20u;
    const uint16_t speed_y = (uint16_t)(top_y + top_h + l.GAP);
    const uint16_t stats_h = 44u; /* Compact 4-column single-row tray */
    const uint16_t content_w = (uint16_t)(DISP_W - 2u * l.M);
    const uint16_t speed_h = (uint16_t)(DISP_H - l.M - stats_h - l.GAP - speed_y);

    l.top_area = (ui_rect_t){0, 0, DISP_W, speed_y};
    l.speed = (ui_rect_t){l.M, speed_y, content_w, speed_h};
    l.speed_in = inset_rect(l.speed, l.ST);
    l.tray = (ui_rect_t){l.M, (uint16_t)(l.speed.y + l.speed.h + l.GAP), content_w, stats_h};
    l.tray_in = inset_rect(l.tray, l.ST);
    return l;
}

static void dash_v2_render_top(ui_render_ctx_t *ctx, const ui_model_t *m,
                               const ui_dash_v2_layout_t *l,
                               uint16_t bg,
                               uint16_t text,
                               uint16_t muted,
                               uint16_t card_fill,
                               uint16_t stroke,
                               uint16_t warn,
                               uint16_t danger,
                               uint16_t ok)
{
    char buf[20];

    ui_draw_rect(ctx, l->top_area, bg);

    uint16_t top_y = l->M;
    uint16_t top_h = 20u;

    /* Small chips for assist + gear. */
    ui_rect_t chip = {l->M, top_y, 56u, top_h};
    draw_outline_panel(ctx, chip, stroke, card_fill, 10u);
    ui_draw_text(ctx, (uint16_t)(chip.x + 8u), (uint16_t)(chip.y + 2u), "AST", muted, card_fill);
    fmt_u32(buf, sizeof(buf), (uint32_t)m->assist_mode);
    ui_draw_text(ctx, (uint16_t)(chip.x + 34u), (uint16_t)(chip.y + 2u), buf, text, card_fill);

    chip.x = (uint16_t)(chip.x + chip.w + 6u);
    chip.w = 40u;
    draw_outline_panel(ctx, chip, stroke, card_fill, 10u);
    ui_draw_text(ctx, (uint16_t)(chip.x + 8u), (uint16_t)(chip.y + 2u), "G", muted, card_fill);
    fmt_u32(buf, sizeof(buf), (uint32_t)m->virtual_gear);
    ui_draw_text(ctx, (uint16_t)(chip.x + 20u), (uint16_t)(chip.y + 2u), buf, text, card_fill);
    uint16_t left_end = (uint16_t)(chip.x + chip.w + 6u);

    /* Right: SOC + battery icon. */
    uint16_t icon_color = ok;
    if (m->soc_pct < 15u)
        icon_color = danger;
    else if (m->soc_pct < 35u)
        icon_color = warn;
    ui_rect_t batt = {(uint16_t)(DISP_W - l->M - 40u), (uint16_t)(top_y + 3u), 38u, 14u};
    ui_draw_battery_icon(ctx, batt, m->soc_pct, icon_color, bg);
    fmt_u32(buf, sizeof(buf), (uint32_t)m->soc_pct);
    uint16_t soc_w = txt_w_est(buf);
    uint16_t soc_x = 0u;
    if (batt.x > (uint16_t)(4u + soc_w))
        soc_x = (uint16_t)(batt.x - 4u - soc_w);
    ui_draw_text(ctx, soc_x, (uint16_t)(top_y + 2u), buf, text, bg);

    /* Center label priority: WALK > CRUISE > limiter > mode. */
    const char *center_str = m->mode ? "PRIVATE" : "LEGAL";
    uint16_t center_color = text;
    if (m->walk_state == 1u) /* WALK_STATE_ACTIVE */
    {
        center_str = "WALK";
        center_color = ok;
    }
    else if (m->cruise_mode != 0u)
    {
        center_str = "CRUISE";
        center_color = ok;
    }
    else if (m->limit_reason != LIMIT_REASON_USER)
    {
        if (m->limit_reason == LIMIT_REASON_LUG)
            center_str = "LUG";
        else if (m->limit_reason == LIMIT_REASON_THERM)
            center_str = "THERM";
        else if (m->limit_reason == LIMIT_REASON_SAG)
            center_str = "SAG";
        center_color = warn;
    }
    uint16_t right_start = (soc_x > 6u) ? (uint16_t)(soc_x - 6u) : soc_x;
    uint16_t avail_l = left_end;
    uint16_t avail_r = right_start;
    if (avail_r < avail_l)
        avail_r = avail_l;
    uint16_t center_w = txt_w_est(center_str);
    uint16_t cx = avail_l;
    if ((uint16_t)(avail_r - avail_l) > center_w)
        cx = (uint16_t)(avail_l + ((avail_r - avail_l) - center_w) / 2u);
    ui_draw_text(ctx, cx, (uint16_t)(top_y + 2u), center_str, center_color, bg);

    if (m->err || m->brake)
        ui_draw_warning_icon(ctx, (uint16_t)(DISP_W - l->M - 14u), (uint16_t)(top_y + 3u), m->err ? danger : warn);
}

static void dash_v2_render_speed_inner(ui_render_ctx_t *ctx, const ui_model_t *m,
                                       const ui_dash_v2_layout_t *l,
                                       uint16_t panel,
                                       uint16_t text,
                                       uint16_t muted,
                                       uint16_t accent,
                                       uint16_t warn,
                                       uint16_t stroke,
                                       uint16_t card_fill)
{
    char buf[20];
    ui_rect_t speed = l->speed;
    ui_rect_t speed_in = l->speed_in;
    ui_draw_round_rect(ctx, speed_in, card_fill, (uint8_t)(l->R - 2u));

    /*
     * Curved power gauge (halo arc) clipped to the speed card.
     * A product-y trick: draw a circle centered below the card, and clip it.
     * This yields a smooth arc without needing a full path renderer.
     */
    uint32_t pct = 0u;
    if (m->limit_power_w)
    {
        pct = (uint32_t)m->power_w * 100u / (uint32_t)m->limit_power_w;
        if (pct > 100u)
            pct = 100u;
    }
    else
    {
        /* Fallback scaling when no limit is known. */
        uint32_t p = m->power_w;
        if (p > 900u)
            p = 900u;
        pct = p * 100u / 900u;
    }

    /* Subtle, curved halo behind digits. Keep inactive close to card_fill. */
    const uint16_t gauge_active = rgb565_lerp(card_fill, (m->limit_reason != LIMIT_REASON_USER) ? warn : accent, 220u);
    const uint16_t gauge_inactive = rgb565_lerp(card_fill, muted, 64u);
    const int16_t gcx = (int16_t)(speed.x + speed.w / 2u);
    const int16_t gcy = (int16_t)(speed.y + speed.h - 34u); /* slightly above bottom */
    const uint16_t outer_r = 110u;
    const uint16_t thick = 8u;
    const int16_t start_deg = 200;     /* left-ish */
    const uint16_t sweep_deg = 140u;   /* sweep over the top */
    const uint16_t active_sweep = (uint16_t)((uint32_t)sweep_deg * pct / 100u);
    ui_draw_ring_gauge(ctx, speed_in,
                       gcx, gcy, outer_r, thick,
                       start_deg, sweep_deg, active_sweep,
                       gauge_active, gauge_inactive, card_fill);

    /* Unit label: above digits, centered (never overlaps). */
    const char *unit = m->units ? "KMH" : "MPH";
    ui_draw_text(ctx, (uint16_t)(speed.x + speed.w / 2u - 14u), (uint16_t)(speed.y + 10u), unit, muted, card_fill);

    /* Big speed digits: centered. */
    uint16_t spd = (uint16_t)(m->speed_dmph / 10u);
    uint8_t digits = (spd >= 100u) ? 3u : ((spd >= 10u) ? 2u : 1u);
    uint8_t scale = 5u;
    uint16_t dw = seg_digit_w(scale);
    uint16_t dgap = (uint16_t)(2u * (uint16_t)scale);
    uint16_t total = (uint16_t)(digits * dw + (digits - 1u) * dgap);
    uint16_t dx0 = (speed.w > total) ? (uint16_t)(speed.x + (speed.w - total) / 2u) : speed.x;
    uint16_t dy0 = (uint16_t)(speed.y + 48u);

    uint16_t digit_shadow = rgb565_dim(accent);
    uint16_t sx = (uint16_t)(dx0 + 2u);
    uint16_t sy = (uint16_t)(dy0 + 2u);
    if (digits == 3u)
    {
        ui_draw_big_digit(ctx, sx, sy, (uint8_t)(spd / 100u), scale, digit_shadow);
        sx = (uint16_t)(sx + dw + dgap);
    }
    if (digits >= 2u)
    {
        ui_draw_big_digit(ctx, sx, sy, (uint8_t)((spd / 10u) % 10u), scale, digit_shadow);
        sx = (uint16_t)(sx + dw + dgap);
    }
    ui_draw_big_digit(ctx, sx, sy, (uint8_t)(spd % 10u), scale, digit_shadow);

    uint16_t dx = dx0;
    if (digits == 3u)
    {
        ui_draw_big_digit(ctx, dx, dy0, (uint8_t)(spd / 100u), scale, accent);
        dx = (uint16_t)(dx + dw + dgap);
    }
    if (digits >= 2u)
    {
        ui_draw_big_digit(ctx, dx, dy0, (uint8_t)((spd / 10u) % 10u), scale, accent);
        dx = (uint16_t)(dx + dw + dgap);
    }
    ui_draw_big_digit(ctx, dx, dy0, (uint8_t)(spd % 10u), scale, accent);

    /* Bottom info row inside speed card. */
    uint16_t info_y = (uint16_t)(speed.y + speed.h - 22u);
    ui_draw_rect(ctx, (ui_rect_t){(uint16_t)(speed.x + 12u), (uint16_t)(info_y - 6u), (uint16_t)(speed.w - 24u), 1u}, stroke);

    ui_draw_text(ctx, (uint16_t)(speed.x + 18u), info_y, "PWR", muted, card_fill);
    fmt_u32(buf, sizeof(buf), (uint32_t)m->power_w);
    ui_draw_text(ctx, (uint16_t)(speed.x + 48u), info_y, buf, text, card_fill);
    ui_draw_text(ctx, (uint16_t)(speed.x + 78u), info_y, "W", muted, card_fill);

    ui_draw_text(ctx, (uint16_t)(speed.x + speed.w / 2u + 6u), info_y, "RNG", muted, card_fill);
    fmt_d10(buf, sizeof(buf), (int32_t)m->range_est_d10);
    ui_draw_text(ctx, (uint16_t)(speed.x + speed.w / 2u + 38u), info_y, buf, text, card_fill);
    ui_draw_text(ctx, (uint16_t)(speed.x + speed.w / 2u + 74u), info_y, m->units ? "KM" : "MI", muted, card_fill);

    /* Range confidence ticks (0..5). */
    uint16_t conf = m->range_confidence;
    uint8_t ticks = (uint8_t)((conf * 5u + 50u) / 100u);
    if (ticks > 5u)
        ticks = 5u;
    uint16_t tx = (uint16_t)(speed.x + speed.w - 10u - 5u * 6u);
    uint16_t ty = (info_y > 16u) ? (uint16_t)(info_y - 16u) : info_y;
    for (uint8_t i = 0; i < 5u; ++i)
    {
        ui_rect_t t = {(uint16_t)(tx + i * 6u), ty, 4u, 2u};
        ui_draw_rect(ctx, t, (i < ticks) ? accent : stroke);
    }
    (void)panel;
}

static void dash_v2_render_tray_inner(ui_render_ctx_t *ctx, const ui_model_t *m,
                                      const ui_dash_v2_layout_t *l,
                                      uint16_t dist_d10,
                                      uint16_t wh_d10,
                                      uint16_t text,
                                      uint16_t muted,
                                      uint16_t stroke,
                                      uint16_t card_fill)
{
    char buf[20];
    ui_rect_t tray = l->tray;
    ui_rect_t tray_in = l->tray_in;

    ui_draw_round_rect(ctx, tray_in, card_fill, (uint8_t)(l->R - 2u));

    /* 4-column layout: VOLT | CUR | TRIP | WH/MI */
    uint16_t col_w = (uint16_t)(tray.w / 4u);
    uint16_t label_y = (uint16_t)(tray.y + 6u);
    uint16_t value_y = (uint16_t)(tray.y + 22u);

    /* Draw 3 vertical dividers between columns */
    for (uint8_t i = 1u; i < 4u; ++i) {
        ui_rect_t vdiv = {
            (uint16_t)(tray.x + i * col_w),
            (uint16_t)(tray.y + 6u),
            1u,
            (uint16_t)(tray.h - 12u)
        };
        ui_draw_rect(ctx, vdiv, stroke);
    }

    /* Column 0: VOLT */
    uint16_t col0_x = (uint16_t)(tray.x + 4u);
    ui_draw_text(ctx, col0_x, label_y, "VOLT", muted, card_fill);
    fmt_d10(buf, sizeof(buf), m->batt_dV);
    ui_draw_text(ctx, col0_x, value_y, buf, text, card_fill);

    /* Column 1: CUR */
    uint16_t col1_x = (uint16_t)(tray.x + col_w + 4u);
    ui_draw_text(ctx, col1_x, label_y, "CUR", muted, card_fill);
    fmt_d10(buf, sizeof(buf), m->batt_dA);
    ui_draw_text(ctx, col1_x, value_y, buf, text, card_fill);

    /* Column 2: TRIP */
    uint16_t col2_x = (uint16_t)(tray.x + 2u * col_w + 4u);
    ui_draw_text(ctx, col2_x, label_y, "TRIP", muted, card_fill);
    fmt_d10(buf, sizeof(buf), (int32_t)dist_d10);
    ui_draw_text(ctx, col2_x, value_y, buf, text, card_fill);

    /* Column 3: WH/MI (efficiency) */
    uint16_t col3_x = (uint16_t)(tray.x + 3u * col_w + 4u);
    ui_draw_text(ctx, col3_x, label_y, m->units ? "WH/K" : "WH/M", muted, card_fill);
    fmt_d10(buf, sizeof(buf), (int32_t)wh_d10);
    ui_draw_text(ctx, col3_x, value_y, buf, text, card_fill);
}

static void dirty_dashboard_v2(ui_dirty_t *d, const ui_model_t *m, const ui_model_t *p)
{
    ui_dash_v2_layout_t l = dash_v2_layout();

    if (m->assist_mode != p->assist_mode || m->virtual_gear != p->virtual_gear || m->soc_pct != p->soc_pct ||
        m->mode != p->mode || m->limit_reason != p->limit_reason || m->err != p->err || m->brake != p->brake ||
        m->walk_state != p->walk_state || m->cruise_mode != p->cruise_mode)
        ui_dirty_add(d, l.top_area);

    if (m->speed_dmph != p->speed_dmph || m->power_w != p->power_w || m->limit_power_w != p->limit_power_w ||
        m->limit_reason != p->limit_reason || m->range_est_d10 != p->range_est_d10 ||
        m->range_confidence != p->range_confidence || m->units != p->units)
        ui_dirty_add(d, l.speed_in);

    if (m->batt_dV != p->batt_dV || m->batt_dA != p->batt_dA ||
        m->trip_distance_mm != p->trip_distance_mm || m->trip_energy_mwh != p->trip_energy_mwh ||
        m->units != p->units)
        ui_dirty_add(d, l.tray_in);
}

static void dirty_trip_summary(ui_dirty_t *d, const ui_model_t *m, const ui_model_t *p)
{
    if (m->trip_distance_mm != p->trip_distance_mm ||
        m->trip_energy_mwh != p->trip_energy_mwh ||
        m->trip_max_speed_dmph != p->trip_max_speed_dmph ||
        m->trip_avg_speed_dmph != p->trip_avg_speed_dmph ||
        m->trip_moving_ms != p->trip_moving_ms ||
        m->trip_assist_ms != p->trip_assist_ms ||
        m->trip_gear_ms != p->trip_gear_ms ||
        m->virtual_gear != p->virtual_gear ||
        m->units != p->units)
    {
        ui_dirty_full(d);
    }
}

static void render_dashboard(ui_render_ctx_t *ctx, const ui_model_t *m, uint16_t dist_d10, uint16_t wh_d10)
{
    /*
     * Dashboard v2 (“product feel”):
     * - reduce the number of boxes (tray + one main card)
     * - strong hierarchy: SPEED dominates, then power/range, then bottom stats
     * - consistent spacing: 4px grid, 12px margin, 2px strokes, 16px radii
     */
    ui_dash_v2_layout_t l = dash_v2_layout();

    const uint16_t bg = ui_color(ctx, UI_COLOR_BG);
    const uint16_t panel = ui_color(ctx, UI_COLOR_PANEL);
    const uint16_t text = ui_color(ctx, UI_COLOR_TEXT);
    const uint16_t muted = ui_color(ctx, UI_COLOR_MUTED);
    const uint16_t accent = ui_color(ctx, UI_COLOR_ACCENT);
    const uint16_t warn = ui_color(ctx, UI_COLOR_WARN);
    const uint16_t danger = ui_color(ctx, UI_COLOR_DANGER);
    const uint16_t ok = ui_color(ctx, UI_COLOR_OK);
    const uint16_t stroke = rgb565_dim(muted);
    const uint16_t shadow = rgb565_dim(panel);
    const uint16_t card_fill = rgb565_lerp(bg, panel, 32u);

    ui_draw_rect(ctx, l.full, bg);

    /* ===== Top status row (no heavy bar) ===== */
    dash_v2_render_top(ctx, m, &l, bg, text, muted, card_fill, stroke, warn, danger, ok);

    /* ===== Speed card ===== */
    ui_panel_style_t card_style = {
        .radius = l.R,
        .border_thick = (uint8_t)l.ST,
        .shadow_dx = 2,
        .shadow_dy = 2,
        .fill = card_fill,
        .border = panel,
        .shadow = shadow,
        .flags = panel_flags_for_theme(m->theme),
    };
    ui_draw_panel(ctx, l.speed, &card_style);
    dash_v2_render_speed_inner(ctx, m, &l, panel, text, muted, accent, warn, stroke, card_fill);

    /* ===== Bottom stats tray (4-column compact row) ===== */
    ui_draw_panel(ctx, l.tray, &card_style);
    dash_v2_render_tray_inner(ctx, m, &l, dist_d10, wh_d10, text, muted, stroke, card_fill);
}

static void render_focus(ui_render_ctx_t *ctx, const ui_model_t *m,
                         uint16_t dist_d10, uint16_t wh_d10)
{
    (void)dist_d10;
    (void)wh_d10;
    ui_rect_t bg = {0, 0, DISP_W, DISP_H};
    ui_draw_rect(ctx, bg, ui_color(ctx, UI_COLOR_BG));
    ui_rect_t panel = {PAD, (uint16_t)(TOP_Y + TOP_H + G), (uint16_t)(DISP_W - 2u * PAD), 160u};
    ui_draw_round_rect(ctx, panel, ui_color(ctx, UI_COLOR_PANEL), 10u);

    uint16_t value = 0;
    const char *unit = "";
    if (m->focus_metric == UI_FOCUS_METRIC_POWER)
    {
        value = m->power_w;
        unit = "W";
    }
    else
    {
        value = (uint16_t)(m->speed_dmph / 10u);
        unit = m->units ? "KMH" : "MPH";
    }
    if (value > 9999u)
        value = 9999u;

    uint8_t digits = 1u;
    if (value >= 1000u)
        digits = 4u;
    else if (value >= 100u)
        digits = 3u;
    else if (value >= 10u)
        digits = 2u;
    uint8_t scale = (digits >= 4u) ? 2u : 3u;
    uint16_t digit_w = seg_digit_w(scale);
    uint16_t spacing = 2u;
    uint16_t total = (uint16_t)(digits * digit_w + (digits - 1u) * spacing);
    uint16_t x = panel.x;
    if (panel.w > total)
        x = (uint16_t)(panel.x + (panel.w - total) / 2u);
    uint16_t digit_h = (uint16_t)(20u * (uint16_t)scale);
    uint16_t y = panel.y;
    if (panel.h > digit_h)
        y = (uint16_t)(panel.y + (panel.h - digit_h) / 2u);

    uint16_t div = 1u;
    for (uint8_t i = 1u; i < digits; ++i)
        div = (uint16_t)(div * 10u);
    uint16_t draw_val = value;
    for (uint8_t i = 0u; i < digits; ++i)
    {
        uint8_t d = (uint8_t)(draw_val / div);
        ui_draw_big_digit(ctx, x, y, d, scale, ui_color(ctx, UI_COLOR_ACCENT));
        x = (uint16_t)(x + digit_w + spacing);
        draw_val = (uint16_t)(draw_val % div);
        div = (uint16_t)((div > 1u) ? (div / 10u) : 1u);
    }

    uint16_t unit_w = txt_w_est(unit);
    uint16_t unit_x = (uint16_t)(panel.x + panel.w - unit_w - 10u);
    ui_draw_text(ctx, unit_x, (uint16_t)(panel.y + 12u), unit, ui_color(ctx, UI_COLOR_TEXT), ui_color(ctx, UI_COLOR_PANEL));

    ui_rect_t chip = {PAD, (uint16_t)(panel.y + panel.h + G), 120u, 24u};
    ui_draw_round_rect(ctx, chip, ui_color(ctx, UI_COLOR_PANEL), 6u);
    ui_draw_value(ctx, (uint16_t)(chip.x + 6u), (uint16_t)(chip.y + 4u), "SOC", m->soc_pct, ui_color(ctx, UI_COLOR_TEXT), ui_color(ctx, UI_COLOR_PANEL));
}

typedef struct {
    ui_rect_t full;
    ui_rect_t header;
    ui_rect_t chip_channel;
    ui_rect_t chip_window;
    ui_rect_t chip_hz;
    ui_rect_t graph;
    ui_rect_t graph_dirty;
    ui_rect_t plot;
} ui_graph_layout_t;

static ui_graph_layout_t graph_layout(void)
{
    ui_graph_layout_t l = {0};
    l.full = (ui_rect_t){0, 0, DISP_W, DISP_H};
    l.header = (ui_rect_t){0, TOP_Y, DISP_W, TOP_H};

    uint16_t y = (uint16_t)(TOP_Y + TOP_H + G);
    l.chip_channel = (ui_rect_t){PAD, y, 72u, 24u};
    l.chip_window = (ui_rect_t){(uint16_t)(PAD + 80u), y, 72u, 24u};
    l.chip_hz = (ui_rect_t){(uint16_t)(PAD + 160u), y, 72u, 24u};
    l.graph = (ui_rect_t){PAD, (uint16_t)(y + 32u), (uint16_t)(DISP_W - 2u * PAD), 208u};
    l.graph_dirty = l.graph;

    if (l.graph_dirty.x + l.graph_dirty.w + 2u <= DISP_W)
        l.graph_dirty.w = (uint16_t)(l.graph_dirty.w + 2u);
    else
        l.graph_dirty.w = (uint16_t)(DISP_W - l.graph_dirty.x);
    if (l.graph_dirty.y + l.graph_dirty.h + 2u <= DISP_H)
        l.graph_dirty.h = (uint16_t)(l.graph_dirty.h + 2u);
    else
        l.graph_dirty.h = (uint16_t)(DISP_H - l.graph_dirty.y);

    l.plot = inset_rect(l.graph, 10u);
    if (l.plot.h > 28u)
    {
        l.plot.y = (uint16_t)(l.plot.y + 8u);
        l.plot.h = (uint16_t)(l.plot.h - 16u);
    }
    return l;
}

static const char *graph_channel_label(uint8_t channel)
{
    switch (channel)
    {
    case UI_GRAPH_CH_POWER: return "W";
    case UI_GRAPH_CH_VOLT: return "V";
    case UI_GRAPH_CH_CAD: return "CAD";
    case UI_GRAPH_CH_SPEED:
    default:
        return "SPD";
    }
}

static void render_graph_channel_chip(ui_render_ctx_t *ctx, const ui_model_t *m,
                                      const ui_graph_layout_t *l,
                                      uint16_t bgc, uint16_t panel,
                                      uint16_t accent)
{
    const char *ch_label = graph_channel_label(m->graph_channel);
    uint16_t chip_active = rgb565_lerp(panel, accent, 180u);
    ui_draw_round_rect(ctx, l->chip_channel, chip_active, 8u);
    ui_draw_text(ctx, (uint16_t)(l->chip_channel.x + 10u),
                 (uint16_t)(l->chip_channel.y + 6u), ch_label, bgc, chip_active);
}

static void render_graph_window_chip(ui_render_ctx_t *ctx, const ui_model_t *m,
                                     const ui_graph_layout_t *l,
                                     uint16_t text, uint16_t panel)
{
    ui_draw_round_rect(ctx, l->chip_window, panel, 8u);
    ui_draw_value(ctx, (uint16_t)(l->chip_window.x + 10u),
                  (uint16_t)(l->chip_window.y + 6u), "WIN", m->graph_window_s, text, panel);
}

static void render_graph_hz_chip(ui_render_ctx_t *ctx, const ui_model_t *m,
                                 const ui_graph_layout_t *l,
                                 uint16_t text, uint16_t panel)
{
    ui_draw_round_rect(ctx, l->chip_hz, panel, 8u);
    ui_draw_value(ctx, (uint16_t)(l->chip_hz.x + 10u),
                  (uint16_t)(l->chip_hz.y + 6u), "HZ", m->graph_sample_hz, text, panel);
}

static void render_graph_panel(ui_render_ctx_t *ctx, const ui_graph_layout_t *l,
                               const ui_panel_style_t *card,
                               uint16_t card_fill, uint16_t stroke,
                               uint16_t accent, uint16_t muted)
{
    ui_draw_panel(ctx, l->graph, card);

    /* Grid lines (subtle): 3 horizontal guides. */
    for (uint8_t i = 1u; i <= 3u; ++i)
    {
        uint16_t gy = (uint16_t)(l->plot.y + (uint16_t)((uint32_t)l->plot.h * (uint32_t)i / 4u));
        ui_draw_rect(ctx, (ui_rect_t){l->plot.x, gy, l->plot.w, 1u}, stroke);
    }

    uint16_t min = 0xFFFFu;
    uint16_t max = 0u;
    uint8_t count = (ctx && ctx->ui) ? ctx->ui->graph_count : 0u;
    uint8_t start = 0u;
    if (ctx && ctx->ui && count)
        start = (uint8_t)((ctx->ui->graph_head + UI_GRAPH_SAMPLES - count) % UI_GRAPH_SAMPLES);

    for (uint8_t i = 0; i < count; ++i)
    {
        uint16_t v = ctx->ui->graph_samples[(uint8_t)((start + i) % UI_GRAPH_SAMPLES)];
        if (v < min)
            min = v;
        if (v > max)
            max = v;
    }
    if (count == 0u)
    {
        min = 0u;
        max = 1u;
    }
    if (max == min)
        max = (uint16_t)(min + 1u);

    uint16_t step = 1u;
    if (count > 1u && l->plot.w > 6u)
        step = (uint16_t)((l->plot.w - 6u) / (uint16_t)(count - 1u));
    if (step == 0u)
        step = 1u;

    const uint16_t bar_color = rgb565_lerp(card_fill, accent, 220u);
    for (uint8_t i = 0; i < count; ++i)
    {
        uint16_t v = ctx->ui->graph_samples[(uint8_t)((start + i) % UI_GRAPH_SAMPLES)];
        uint16_t h = (uint16_t)((uint32_t)(v - min) * (l->plot.h - 2u) / (uint32_t)(max - min));
        ui_rect_t bar = {
            (uint16_t)(l->plot.x + 2u + i * step),
            (uint16_t)(l->plot.y + l->plot.h - 1u - h),
            2u,
            (uint16_t)(h ? h : 1u)
        };
        if (bar.x + bar.w < l->plot.x + l->plot.w - 1u)
            ui_draw_rect(ctx, bar, bar_color);
    }

    /* Corner labels. */
    ui_draw_value(ctx, (uint16_t)(l->graph.x + 12u), (uint16_t)(l->graph.y + l->graph.h - 20u), "MIN", min, muted, card_fill);
    ui_draw_value(ctx, (uint16_t)(l->graph.x + l->graph.w - 72u), (uint16_t)(l->graph.y + 10u), "MAX", max, muted, card_fill);
}

static void render_graphs(ui_render_ctx_t *ctx, const ui_model_t *m,
                          uint16_t dist_d10, uint16_t wh_d10)
{
    (void)dist_d10;
    (void)wh_d10;
    ui_graph_layout_t l = graph_layout();
    const uint16_t bgc = ui_color(ctx, UI_COLOR_BG);
    const uint16_t panel = ui_color(ctx, UI_COLOR_PANEL);
    const uint16_t text = ui_color(ctx, UI_COLOR_TEXT);
    const uint16_t muted = ui_color(ctx, UI_COLOR_MUTED);
    const uint16_t accent = ui_color(ctx, UI_COLOR_ACCENT);
    const uint16_t stroke = rgb565_dim(muted);
    const uint16_t shadow = rgb565_dim(panel);
    const uint16_t card_fill = rgb565_lerp(bgc, panel, 32u);

    ui_draw_rect(ctx, l.full, bgc);
    render_header_icon(ctx, "GRAPHS", UI_ICON_GRAPH);

    ui_panel_style_t card = {
        .radius = 10u,
        .border_thick = 1u,
        .shadow_dx = 2,
        .shadow_dy = 2,
        .fill = card_fill,
        .border = panel,
        .shadow = shadow,
    };

    render_graph_channel_chip(ctx, m, &l, bgc, panel, accent);
    render_graph_window_chip(ctx, m, &l, text, panel);
    render_graph_hz_chip(ctx, m, &l, text, panel);
    render_graph_panel(ctx, &l, &card, card_fill, stroke, accent, muted);
}

static void draw_trip_card(ui_render_ctx_t *ctx, ui_rect_t r, const ui_panel_style_t *card,
                           const char *label, const char *value, const char *unit,
                           uint16_t text, uint16_t muted, uint16_t stroke, uint16_t fill)
{
    ui_draw_panel(ctx, r, card);
    ui_draw_text(ctx, (uint16_t)(r.x + 12u), (uint16_t)(r.y + 8u), label, muted, fill);
    ui_draw_rect(ctx, (ui_rect_t){(uint16_t)(r.x + 12u), (uint16_t)(r.y + 24u), (uint16_t)(r.w - 24u), 1u}, stroke);
    if (value)
        ui_draw_text(ctx, (uint16_t)(r.x + 12u), (uint16_t)(r.y + 30u), value, text, fill);
    if (unit && unit[0])
    {
        uint16_t uw = txt_w_est(unit);
        uint16_t ux = (uw + 12u < r.w) ? (uint16_t)(r.x + r.w - 12u - uw) : (uint16_t)(r.x + r.w - 12u);
        ui_draw_text(ctx, ux, (uint16_t)(r.y + 30u), unit, muted, fill);
    }
}

static void render_trip_summary(ui_render_ctx_t *ctx, const ui_model_t *m,
                                uint16_t dist_d10, uint16_t wh_d10)
{
    const uint16_t bgc = ui_color(ctx, UI_COLOR_BG);
    const uint16_t panel = ui_color(ctx, UI_COLOR_PANEL);
    const uint16_t text = ui_color(ctx, UI_COLOR_TEXT);
    const uint16_t muted = ui_color(ctx, UI_COLOR_MUTED);
    const uint16_t shadow = rgb565_dim(panel);
    const uint16_t card_fill = rgb565_lerp(bgc, panel, 32u);
    const uint16_t stroke = rgb565_dim(muted);

    ui_draw_rect(ctx, (ui_rect_t){0, 0, DISP_W, DISP_H}, bgc);
    render_header_icon(ctx, "TRIP", UI_ICON_TRIP);

    ui_panel_style_t card = {
        .radius = 10u,
        .border_thick = 1u,
        .shadow_dx = 2,
        .shadow_dy = 2,
        .fill = card_fill,
        .border = panel,
        .shadow = shadow,
    };

    const char *dist_unit = m->units ? "KM" : "MI";
    const char *speed_unit = m->units ? "KMH" : "MPH";
    const char *eff_label = m->units ? "WH/KM" : "WH/MI";

    uint16_t y = (uint16_t)(TOP_Y + TOP_H + G);
    uint16_t gap = 8u;
    uint16_t cw = (uint16_t)((DISP_W - 2u * PAD - gap) / 2u);
    uint16_t ch = 54u;
    ui_rect_t r0l = {PAD, y, cw, ch};
    ui_rect_t r0r = {(uint16_t)(PAD + cw + gap), y, cw, ch};
    ui_rect_t r1l = {PAD, (uint16_t)(y + ch + gap), cw, ch};
    ui_rect_t r1r = {(uint16_t)(PAD + cw + gap), (uint16_t)(y + ch + gap), cw, ch};
    ui_rect_t r2l = {PAD, (uint16_t)(y + 2u * (ch + gap)), cw, ch};
    ui_rect_t r2r = {(uint16_t)(PAD + cw + gap), (uint16_t)(y + 2u * (ch + gap)), cw, ch};
    ui_rect_t r3l = {PAD, (uint16_t)(y + 3u * (ch + gap)), cw, ch};
    ui_rect_t r3r = {(uint16_t)(PAD + cw + gap), (uint16_t)(y + 3u * (ch + gap)), cw, ch};

    char buf[16];
    fmt_d10(buf, sizeof(buf), (int32_t)dist_d10);
    draw_trip_card(ctx, r0l, &card, "DIST", buf, dist_unit, text, muted, stroke, card_fill);

    fmt_time_hhmm(buf, sizeof(buf), m->trip_moving_ms);
    draw_trip_card(ctx, r0r, &card, "MOVE", buf, NULL, text, muted, stroke, card_fill);

    fmt_d10(buf, sizeof(buf), (int32_t)m->trip_avg_speed_dmph);
    draw_trip_card(ctx, r1l, &card, "AVG", buf, speed_unit, text, muted, stroke, card_fill);

    fmt_d10(buf, sizeof(buf), (int32_t)m->trip_max_speed_dmph);
    draw_trip_card(ctx, r1r, &card, "MAX", buf, speed_unit, text, muted, stroke, card_fill);

    {
        int32_t wh_d10_local = (int32_t)((uint32_t)m->trip_energy_mwh / 100u);
        fmt_d10(buf, sizeof(buf), wh_d10_local);
        draw_trip_card(ctx, r2l, &card, "ENERGY", buf, "Wh", text, muted, stroke, card_fill);
    }

    fmt_d10(buf, sizeof(buf), (int32_t)wh_d10);
    draw_trip_card(ctx, r2r, &card, eff_label, buf, NULL, text, muted, stroke, card_fill);

    fmt_time_hhmm(buf, sizeof(buf), m->trip_assist_ms);
    draw_trip_card(ctx, r3l, &card, "ASSIST", buf, NULL, text, muted, stroke, card_fill);

    fmt_time_hhmm(buf, sizeof(buf), m->trip_gear_ms);
    {
        char gear_unit[8];
        char gear_num[6];
        fmt_u32(gear_num, sizeof(gear_num), (uint32_t)m->virtual_gear);
        gear_unit[0] = 'G';
        size_t i = 0u;
        while (gear_num[i] && (i + 2u) < sizeof(gear_unit))
        {
            gear_unit[i + 1u] = gear_num[i];
            i++;
        }
        gear_unit[i + 1u] = 0;
        draw_trip_card(ctx, r3r, &card, "GEAR", buf, gear_unit, text, muted, stroke, card_fill);
    }
}

static void render_profiles(ui_render_ctx_t *ctx, const ui_model_t *m,
                            uint16_t dist_d10, uint16_t wh_d10)
{
    (void)dist_d10;
    (void)wh_d10;
    const uint16_t bgc = ui_color(ctx, UI_COLOR_BG);
    const uint16_t panel = ui_color(ctx, UI_COLOR_PANEL);
    const uint16_t text = ui_color(ctx, UI_COLOR_TEXT);
    const uint16_t muted = ui_color(ctx, UI_COLOR_MUTED);
    const uint16_t accent = ui_color(ctx, UI_COLOR_ACCENT);
    const uint16_t shadow = rgb565_dim(panel);
    const uint16_t card_fill = rgb565_lerp(bgc, panel, 32u);

    ui_draw_rect(ctx, (ui_rect_t){0, 0, DISP_W, DISP_H}, bgc);
    render_header_icon(ctx, "PROFILES", UI_ICON_PROFILE);

    ui_panel_style_t card = {
        .radius = 10u,
        .border_thick = 1u,
        .shadow_dx = 2,
        .shadow_dy = 2,
        .fill = card_fill,
        .border = panel,
        .shadow = shadow,
        .flags = panel_flags_for_theme(m->theme),
    };

    const char *profiles[] = {"COMMUTE", "TRAIL", "CARGO", "RAIN", "VALET"};
    uint8_t count = (uint8_t)(sizeof(profiles) / sizeof(profiles[0]));

    uint16_t y = (uint16_t)(TOP_Y + TOP_H + G);
    ui_rect_t list = {PAD, y, 112u, 212u};
    ui_rect_t detail = {(uint16_t)(PAD + list.w + G), y, (uint16_t)(DISP_W - 2u * PAD - list.w - G), 212u};
    ui_draw_panel(ctx, list, &card);
    ui_draw_panel(ctx, detail, &card);

    uint16_t sel_fill = rgb565_lerp(card_fill, accent, 36u);
    uint16_t sel_text = bgc;
    uint8_t sel_idx = (m->profile_select < count) ? m->profile_select : 0u;
    uint8_t focus = (m->profile_focus < UI_PROFILE_FOCUS_COUNT) ? m->profile_focus : UI_PROFILE_FOCUS_LIST;
    for (uint8_t i = 0; i < count; ++i)
    {
        ui_rect_t row = {(uint16_t)(list.x + 8u), (uint16_t)(list.y + 10u + i * 38u), (uint16_t)(list.w - 16u), 28u};
        uint8_t active = (m->profile_id == i) ? 1u : 0u;
        uint8_t selected = (sel_idx == i) ? 1u : 0u;
        uint16_t fill = card_fill;
        uint16_t fg = text;
        if (selected && focus == UI_PROFILE_FOCUS_LIST)
        {
            fill = sel_fill;
            fg = sel_text;
        }
        if (selected && focus != UI_PROFILE_FOCUS_LIST)
            draw_outline_panel(ctx, row, accent, fill, 10u);
        else
            ui_draw_round_rect(ctx, row, fill, 10u);
        if (active)
            ui_draw_rect(ctx, (ui_rect_t){(uint16_t)(row.x + 2u), (uint16_t)(row.y + 4u), 4u, 20u}, accent);
        ui_draw_text(ctx, (uint16_t)(row.x + 10u), (uint16_t)(row.y + 8u), profiles[i], fg, fill);
    }

    /* Right details panel: a few "at a glance" controls. */
    ui_draw_text(ctx, (uint16_t)(detail.x + 12u), (uint16_t)(detail.y + 12u), "ACTIVE", muted, card_fill);
    const char *pname = profiles[m->profile_id % count];
    ui_draw_text(ctx, (uint16_t)(detail.x + 12u), (uint16_t)(detail.y + 30u), pname, text, card_fill);

    uint16_t inner_x = (uint16_t)(detail.x + 8u);
    uint16_t inner_w = (detail.w > 16u) ? (uint16_t)(detail.w - 16u) : detail.w;
    uint16_t gear_y = (uint16_t)(detail.y + 52u);
    ui_rect_t gear = {inner_x, gear_y, inner_w, 44u};
    if (focus != UI_PROFILE_FOCUS_LIST)
        draw_outline_panel(ctx, gear, accent, panel, 10u);
    else
        ui_draw_round_rect(ctx, gear, panel, 10u);
    ui_draw_text(ctx, (uint16_t)(gear.x + 6u), (uint16_t)(gear.y + 6u), "GEAR", muted, panel);
    ui_draw_value(ctx, (uint16_t)(gear.x + 6u), (uint16_t)(gear.y + 20u), "G", m->virtual_gear, text, panel);
    ui_draw_value(ctx, (uint16_t)(gear.x + gear.w - 36u), (uint16_t)(gear.y + 20u), "OF", m->gear_count, muted, panel);

    uint16_t chip_y = (uint16_t)(gear.y + gear.h + 8u);
    uint16_t chip_h = 28u;
    uint16_t chip_gap = 6u;
    uint16_t chip_on = rgb565_lerp(panel, accent, 96u);
    uint16_t chip_off = panel;
    uint16_t chip_fg_on = bgc;
    uint16_t chip_fg_off = text;

    ui_rect_t chip = {inner_x, chip_y, inner_w, chip_h};
    uint8_t focus_min = (focus == UI_PROFILE_FOCUS_GEAR_MIN) ? 1u : 0u;
    ui_draw_round_rect(ctx, chip, focus_min ? chip_on : chip_off, 10u);
    ui_draw_value(ctx, (uint16_t)(chip.x + 6u), (uint16_t)(chip.y + 6u), "MIN", m->gear_min_pct,
                  focus_min ? chip_fg_on : chip_fg_off, focus_min ? chip_on : chip_off);

    chip.y = (uint16_t)(chip.y + chip_h + chip_gap);
    uint8_t focus_max = (focus == UI_PROFILE_FOCUS_GEAR_MAX) ? 1u : 0u;
    ui_draw_round_rect(ctx, chip, focus_max ? chip_on : chip_off, 10u);
    ui_draw_value(ctx, (uint16_t)(chip.x + 6u), (uint16_t)(chip.y + 6u), "MAX", m->gear_max_pct,
                  focus_max ? chip_fg_on : chip_fg_off, focus_max ? chip_on : chip_off);

    chip.y = (uint16_t)(chip.y + chip_h + chip_gap);
    uint8_t focus_shape = (focus == UI_PROFILE_FOCUS_GEAR_SHAPE) ? 1u : 0u;
    uint16_t shape_fg_label = focus_shape ? chip_fg_on : muted;
    uint16_t shape_fg = focus_shape ? chip_fg_on : chip_fg_off;
    ui_draw_round_rect(ctx, chip, focus_shape ? chip_on : chip_off, 10u);
    ui_draw_text(ctx, (uint16_t)(chip.x + 6u), (uint16_t)(chip.y + 6u), "SHAPE", shape_fg_label,
                 focus_shape ? chip_on : chip_off);
    const char *shape = m->gear_shape ? "EXP" : "LIN";
    uint16_t sw = txt_w_est(shape);
    uint16_t sx = (chip.w > (uint16_t)(sw + 8u)) ? (uint16_t)(chip.x + chip.w - sw - 8u) : chip.x;
    ui_draw_text(ctx, sx, (uint16_t)(chip.y + 6u), shape, shape_fg, focus_shape ? chip_on : chip_off);
}

static void render_settings(ui_render_ctx_t *ctx, const ui_model_t *m,
                            uint16_t dist_d10, uint16_t wh_d10)
{
    (void)dist_d10;
    (void)wh_d10;
    const uint16_t bgc = ui_color(ctx, UI_COLOR_BG);
    const uint16_t panel = ui_color(ctx, UI_COLOR_PANEL);
    const uint16_t text = ui_color(ctx, UI_COLOR_TEXT);
    const uint16_t muted = ui_color(ctx, UI_COLOR_MUTED);
    const uint16_t accent = ui_color(ctx, UI_COLOR_ACCENT);
    const uint16_t stroke = rgb565_dim(muted);
    const uint16_t shadow = rgb565_dim(panel);
    const uint16_t card_fill = rgb565_lerp(bgc, panel, 32u);

    ui_draw_rect(ctx, (ui_rect_t){0, 0, DISP_W, DISP_H}, bgc);
    render_header_icon(ctx, "SETTINGS", UI_ICON_SETTINGS);

    ui_panel_style_t card = {
        .radius = 10u,
        .border_thick = 1u,
        .shadow_dx = 2,
        .shadow_dy = 2,
        .fill = card_fill,
        .border = panel,
        .shadow = shadow,
        .flags = panel_flags_for_theme(m->theme),
    };

    uint16_t y = (uint16_t)(TOP_Y + TOP_H + G);
    ui_rect_t list = {PAD, y, (uint16_t)(DISP_W - 2u * PAD), 212u};
    ui_draw_panel(ctx, list, &card);

    const uint16_t sel_fill = rgb565_lerp(card_fill, accent, 28u);
    const uint8_t count = UI_SETTINGS_ITEM_COUNT;
    const uint16_t row_h = 28u;
    const uint16_t row_pitch = 32u;
    const uint16_t row_y0 = 10u;
    for (uint8_t idx = 0; idx < count; ++idx)
    {
        ui_rect_t row = {(uint16_t)(list.x + 8u), (uint16_t)(list.y + row_y0 + idx * row_pitch), (uint16_t)(list.w - 16u), row_h};
        uint8_t sel = (m->settings_index == idx) ? 1u : 0u;
        ui_draw_drop_shadow(ctx, row, 10u, 0, 2, shadow);
        ui_draw_round_rect(ctx, row, sel ? sel_fill : card_fill, 10u);

        if (idx != 0u)
        {
            ui_draw_rect(ctx, (ui_rect_t){(uint16_t)(row.x + 6u), (uint16_t)(row.y - 4u), (uint16_t)(row.w - 12u), 1u}, stroke);
        }

        const char *label = "";
        const char *value = "";
        char tmp[16];
        tmp[0] = 0;
        switch (idx)
        {
        case UI_SETTINGS_ITEM_WIZARD:
            label = "WIZARD";
            value = "START";
            break;
        case UI_SETTINGS_ITEM_UNITS:
            label = "UNITS";
            value = m->units ? "KM/H" : "MPH";
            break;
        case UI_SETTINGS_ITEM_BUTTON_MAP:
            label = "BTN MAP";
            fmt_u32(tmp, sizeof(tmp), (uint32_t)m->button_map);
            value = tmp;
            break;
        case UI_SETTINGS_ITEM_THEME:
            label = "THEME";
            value = theme_name(m->theme);
            break;
        case UI_SETTINGS_ITEM_MODE:
            label = "MODE";
            value = m->mode ? "PRIVATE" : "LEGAL";
            break;
        case UI_SETTINGS_ITEM_PIN:
            label = "PIN";
            fmt_u32_pad4(tmp, sizeof(tmp), (uint32_t)m->pin_code);
            value = tmp;
            break;
        default:
            break;
        }

        ui_draw_text(ctx, (uint16_t)(row.x + 10u), (uint16_t)(row.y + 8u), label, text, sel ? sel_fill : card_fill);
        uint16_t vw = txt_w_est(value);
        uint16_t vx = (row.w > (uint16_t)(vw + 10u)) ? (uint16_t)(row.x + row.w - vw - 10u) : row.x;
        ui_draw_text(ctx, vx, (uint16_t)(row.y + 8u), value, sel ? bgc : muted, sel ? sel_fill : card_fill);
    }
}

static void render_cruise(ui_render_ctx_t *ctx, const ui_model_t *m,
                          uint16_t dist_d10, uint16_t wh_d10)
{
    (void)dist_d10;
    (void)wh_d10;
    const uint16_t bgc = ui_color(ctx, UI_COLOR_BG);
    const uint16_t panel = ui_color(ctx, UI_COLOR_PANEL);
    const uint16_t text = ui_color(ctx, UI_COLOR_TEXT);
    const uint16_t muted = ui_color(ctx, UI_COLOR_MUTED);
    const uint16_t accent = ui_color(ctx, UI_COLOR_ACCENT);
    const uint16_t warn = ui_color(ctx, UI_COLOR_WARN);
    const uint16_t ok = ui_color(ctx, UI_COLOR_OK);
    const uint16_t shadow = rgb565_dim(panel);
    const uint16_t card_fill = rgb565_lerp(bgc, panel, 32u);

    ui_draw_rect(ctx, (ui_rect_t){0, 0, DISP_W, DISP_H}, bgc);
    render_header_icon(ctx, "CRUISE", UI_ICON_CRUISE);

    ui_panel_style_t card = {
        .radius = 10u,
        .border_thick = 1u,
        .shadow_dx = 2,
        .shadow_dy = 2,
        .fill = card_fill,
        .border = panel,
        .shadow = shadow,
        .flags = panel_flags_for_theme(m->theme),
    };

    uint16_t y = (uint16_t)(TOP_Y + TOP_H + G);
    ui_rect_t hero = {PAD, y, (uint16_t)(DISP_W - 2u * PAD), 120u};
    ui_draw_panel(ctx, hero, &card);

    const char *mode = "OFF";
    const char *unit = "";
    uint16_t set_val = 0u;
    if (m->cruise_mode == 1u)
    {
        mode = "SPEED";
        unit = m->units ? "KMH" : "MPH";
        set_val = (uint16_t)(m->cruise_set_dmph / 10u);
    }
    else if (m->cruise_mode == 2u)
    {
        mode = "POWER";
        unit = "W";
        set_val = m->cruise_set_power_w;
    }

    ui_draw_text(ctx, (uint16_t)(hero.x + 12u), (uint16_t)(hero.y + 10u), mode, muted, card_fill);
    ui_draw_text(ctx, (uint16_t)(hero.x + hero.w - 48u), (uint16_t)(hero.y + 10u), unit, muted, card_fill);

    /* Status chip */
    ui_rect_t st = {(uint16_t)(hero.x + hero.w - 86u), (uint16_t)(hero.y + 34u), 74u, 22u};
    uint16_t st_fill = m->cruise_resume_available ? rgb565_lerp(panel, ok, 200u) : rgb565_lerp(panel, warn, 180u);
    ui_draw_round_rect(ctx, st, st_fill, 10u);
    ui_draw_text(ctx, (uint16_t)(st.x + 10u), (uint16_t)(st.y + 6u),
                 m->cruise_resume_available ? "READY" : "BLOCK", bgc, st_fill);

    /* Big setpoint digits */
    uint16_t spd = set_val;
    uint8_t digits = (spd >= 100u) ? 3u : ((spd >= 10u) ? 2u : 1u);
    uint8_t scale = (digits >= 3u) ? 4u : 5u;
    uint16_t dw = seg_digit_w(scale);
    uint16_t gap = (uint16_t)(2u * (uint16_t)scale);
    uint16_t total = (uint16_t)(digits * dw + (digits - 1u) * gap);
    uint16_t dx0 = (hero.w > total) ? (uint16_t)(hero.x + (hero.w - total) / 2u) : hero.x;
    uint16_t dy0 = (uint16_t)(hero.y + 56u);

    uint16_t digit_shadow = rgb565_dim(accent);
    uint16_t sx = (uint16_t)(dx0 + 2u);
    uint16_t sy = (uint16_t)(dy0 + 2u);
    if (digits == 3u)
    {
        ui_draw_big_digit(ctx, sx, sy, (uint8_t)(spd / 100u), scale, digit_shadow);
        sx = (uint16_t)(sx + dw + gap);
    }
    if (digits >= 2u)
    {
        ui_draw_big_digit(ctx, sx, sy, (uint8_t)((spd / 10u) % 10u), scale, digit_shadow);
        sx = (uint16_t)(sx + dw + gap);
    }
    ui_draw_big_digit(ctx, sx, sy, (uint8_t)(spd % 10u), scale, digit_shadow);

    uint16_t dx = dx0;
    if (digits == 3u)
    {
        ui_draw_big_digit(ctx, dx, dy0, (uint8_t)(spd / 100u), scale, accent);
        dx = (uint16_t)(dx + dw + gap);
    }
    if (digits >= 2u)
    {
        ui_draw_big_digit(ctx, dx, dy0, (uint8_t)((spd / 10u) % 10u), scale, accent);
        dx = (uint16_t)(dx + dw + gap);
    }
    ui_draw_big_digit(ctx, dx, dy0, (uint8_t)(spd % 10u), scale, accent);

    /* Resume reason (friendly label) */
    const char *reason = "OK";
    switch (m->cruise_resume_reason)
    {
        case 1u: reason = "BRAKE"; break;
        case 2u: reason = "SPEED"; break;
        case 3u: reason = "PEDAL"; break;
        case 4u: reason = "LIMIT"; break;
        case 5u: reason = "FAULT"; break;
        default: reason = "OK"; break;
    }

    ui_rect_t footer = {PAD, (uint16_t)(hero.y + hero.h + G), (uint16_t)(DISP_W - 2u * PAD), 58u};
    ui_draw_panel(ctx, footer, &card);
    ui_draw_text(ctx, (uint16_t)(footer.x + 12u), (uint16_t)(footer.y + 12u), "RESUME", muted, card_fill);
    ui_draw_text(ctx, (uint16_t)(footer.x + 12u), (uint16_t)(footer.y + 30u), reason, text, card_fill);
    ui_draw_text(ctx, (uint16_t)(footer.x + footer.w - 84u), (uint16_t)(footer.y + 30u),
                 m->cruise_resume_available ? "AVAILABLE" : "BLOCKED", m->cruise_resume_available ? ok : warn, card_fill);
}

static void render_battery_screen(ui_render_ctx_t *ctx, const ui_model_t *m,
                                  uint16_t dist_d10, uint16_t wh_d10)
{
    (void)dist_d10;
    (void)wh_d10;
    const uint16_t bgc = ui_color(ctx, UI_COLOR_BG);
    const uint16_t panel = ui_color(ctx, UI_COLOR_PANEL);
    const uint16_t text = ui_color(ctx, UI_COLOR_TEXT);
    const uint16_t muted = ui_color(ctx, UI_COLOR_MUTED);
    const uint16_t accent = ui_color(ctx, UI_COLOR_ACCENT);
    const uint16_t warn = ui_color(ctx, UI_COLOR_WARN);
    const uint16_t danger = ui_color(ctx, UI_COLOR_DANGER);
    const uint16_t shadow = rgb565_dim(panel);
    const uint16_t card_fill = rgb565_lerp(bgc, panel, 32u);
    const uint16_t stroke = rgb565_dim(muted);

    ui_draw_rect(ctx, (ui_rect_t){0, 0, DISP_W, DISP_H}, bgc);
    render_header_icon(ctx, "BATTERY", UI_ICON_BATTERY);

    ui_panel_style_t card = {
        .radius = 10u,
        .border_thick = 1u,
        .shadow_dx = 2,
        .shadow_dy = 2,
        .fill = card_fill,
        .border = panel,
        .shadow = shadow,
        .flags = panel_flags_for_theme(m->theme),
    };

    uint16_t y = (uint16_t)(TOP_Y + TOP_H + G);
    ui_rect_t hero = {PAD, y, (uint16_t)(DISP_W - 2u * PAD), 132u};
    ui_draw_panel(ctx, hero, &card);

    /* SOC ring gauge (full circle) */
    uint8_t soc = m->soc_pct;
    if (soc > 100u)
        soc = 100u;
    uint16_t soc_color = accent;
    if (soc < 20u)
        soc_color = danger;
    else if (soc < 40u)
        soc_color = warn;
    uint16_t inactive = rgb565_lerp(card_fill, muted, 64u);
    ui_rect_t clip = inset_rect(hero, 6u);
    int16_t cx = (int16_t)(hero.x + 60u);
    int16_t cy = (int16_t)(hero.y + 72u);
    uint16_t outer_r = 52u;
    uint16_t thick = 10u;
    uint16_t sweep = 360u;
    uint16_t active_sweep = (uint16_t)((uint32_t)sweep * soc / 100u);
    ui_draw_ring_gauge(ctx, clip, cx, cy, outer_r, thick, -90, sweep, active_sweep,
                       rgb565_lerp(card_fill, soc_color, 220u), inactive, card_fill);

    /* SOC digits inside ring */
    uint8_t sd = (soc >= 100u) ? 3u : ((soc >= 10u) ? 2u : 1u);
    uint8_t scale = (soc >= 100u) ? 2u : 3u;
    uint16_t dw = seg_digit_w(scale);
    uint16_t gap = (uint16_t)(2u * (uint16_t)scale);
    uint16_t total = (uint16_t)(sd * dw + (sd - 1u) * gap);
    uint16_t dx0 = (uint16_t)((int)cx - (int)total / 2);
    uint16_t dy0 = (uint16_t)((int)cy - 18);
    uint16_t digit_shadow = rgb565_dim(soc_color);
    uint16_t sx = (uint16_t)(dx0 + 2u);
    uint16_t sy = (uint16_t)(dy0 + 2u);
    if (sd == 3u)
    {
        ui_draw_big_digit(ctx, sx, sy, 1u, scale, digit_shadow);
        sx = (uint16_t)(sx + dw + gap);
        ui_draw_big_digit(ctx, sx, sy, 0u, scale, digit_shadow);
        sx = (uint16_t)(sx + dw + gap);
        ui_draw_big_digit(ctx, sx, sy, 0u, scale, digit_shadow);
    }
    else if (sd == 2u)
    {
        ui_draw_big_digit(ctx, sx, sy, (uint8_t)(soc / 10u), scale, digit_shadow);
        sx = (uint16_t)(sx + dw + gap);
        ui_draw_big_digit(ctx, sx, sy, (uint8_t)(soc % 10u), scale, digit_shadow);
    }
    else
    {
        ui_draw_big_digit(ctx, sx, sy, (uint8_t)(soc % 10u), scale, digit_shadow);
    }

    uint16_t dx = dx0;
    if (sd == 3u)
    {
        ui_draw_big_digit(ctx, dx, dy0, 1u, scale, soc_color);
        dx = (uint16_t)(dx + dw + gap);
        ui_draw_big_digit(ctx, dx, dy0, 0u, scale, soc_color);
        dx = (uint16_t)(dx + dw + gap);
        ui_draw_big_digit(ctx, dx, dy0, 0u, scale, soc_color);
    }
    else if (sd == 2u)
    {
        ui_draw_big_digit(ctx, dx, dy0, (uint8_t)(soc / 10u), scale, soc_color);
        dx = (uint16_t)(dx + dw + gap);
        ui_draw_big_digit(ctx, dx, dy0, (uint8_t)(soc % 10u), scale, soc_color);
    }
    else
    {
        ui_draw_big_digit(ctx, dx, dy0, (uint8_t)(soc % 10u), scale, soc_color);
    }
    ui_draw_text(ctx, (uint16_t)(dx0 + total + 4u), (uint16_t)(dy0 + 22u), "%", muted, card_fill);

    /* Right-side stats */
    ui_rect_t stat = {(uint16_t)(hero.x + 120u), (uint16_t)(hero.y + 18u), (uint16_t)(hero.w - 132u), 92u};
    ui_draw_round_rect(ctx, stat, panel, 10u);
    ui_draw_text(ctx, (uint16_t)(stat.x + 10u), (uint16_t)(stat.y + 8u), "VOLT", muted, panel);
    ui_draw_value(ctx, (uint16_t)(stat.x + 10u), (uint16_t)(stat.y + 22u), "", m->batt_dV, text, panel);
    ui_draw_text(ctx, (uint16_t)(stat.x + 10u), (uint16_t)(stat.y + 40u), "CUR", muted, panel);
    ui_draw_value(ctx, (uint16_t)(stat.x + 10u), (uint16_t)(stat.y + 54u), "", m->batt_dA, text, panel);
    ui_draw_rect(ctx, (ui_rect_t){(uint16_t)(stat.x + 8u), (uint16_t)(stat.y + 36u), (uint16_t)(stat.w - 16u), 1u}, stroke);

    /* Range + sag (bottom card) */
    ui_rect_t bottom = {PAD, (uint16_t)(hero.y + hero.h + G), (uint16_t)(DISP_W - 2u * PAD), 70u};
    ui_draw_panel(ctx, bottom, &card);
    ui_draw_text(ctx, (uint16_t)(bottom.x + 12u), (uint16_t)(bottom.y + 10u), "RANGE", muted, card_fill);
    {
        char buf[16];
        fmt_d10(buf, sizeof(buf), (int32_t)m->range_est_d10);
        ui_draw_text(ctx, (uint16_t)(bottom.x + 12u), (uint16_t)(bottom.y + 30u), buf, text, card_fill);
        ui_draw_text(ctx, (uint16_t)(bottom.x + 72u), (uint16_t)(bottom.y + 30u), m->units ? "KM" : "MI", muted, card_fill);
    }
    ui_draw_value(ctx, (uint16_t)(bottom.x + bottom.w - 96u), (uint16_t)(bottom.y + 10u), "SAG dV", m->sag_margin_dV, muted, card_fill);
    /* Confidence bar (5 ticks) */
    {
        uint8_t ticks = (uint8_t)((m->range_confidence * 5u + 50u) / 100u);
        if (ticks > 5u)
            ticks = 5u;
        uint16_t bx = (uint16_t)(bottom.x + bottom.w - 96u);
        uint16_t by = (uint16_t)(bottom.y + 40u);
        for (uint8_t i = 0; i < 5u; ++i)
        {
            ui_rect_t t = {(uint16_t)(bx + i * 10u), by, 8u, 6u};
            ui_draw_round_rect(ctx, t, (i < ticks) ? soc_color : stroke, 3u);
        }
    }
}

static void render_thermal(ui_render_ctx_t *ctx, const ui_model_t *m,
                           uint16_t dist_d10, uint16_t wh_d10)
{
    (void)dist_d10;
    (void)wh_d10;
    const uint16_t bgc = ui_color(ctx, UI_COLOR_BG);
    const uint16_t panel = ui_color(ctx, UI_COLOR_PANEL);
    const uint16_t text = ui_color(ctx, UI_COLOR_TEXT);
    const uint16_t muted = ui_color(ctx, UI_COLOR_MUTED);
    const uint16_t accent = ui_color(ctx, UI_COLOR_ACCENT);
    const uint16_t warn = ui_color(ctx, UI_COLOR_WARN);
    const uint16_t danger = ui_color(ctx, UI_COLOR_DANGER);
    const uint16_t shadow = rgb565_dim(panel);
    const uint16_t card_fill = rgb565_lerp(bgc, panel, 32u);
    const uint16_t stroke = rgb565_dim(muted);

    ui_draw_rect(ctx, (ui_rect_t){0, 0, DISP_W, DISP_H}, bgc);
    render_header_icon(ctx, "THERMAL", UI_ICON_THERMO);

    ui_panel_style_t card = {
        .radius = 10u,
        .border_thick = 1u,
        .shadow_dx = 2,
        .shadow_dy = 2,
        .fill = card_fill,
        .border = panel,
        .shadow = shadow,
        .flags = panel_flags_for_theme(m->theme),
    };

    uint16_t y = (uint16_t)(TOP_Y + TOP_H + G);
    ui_rect_t hero = {PAD, y, (uint16_t)(DISP_W - 2u * PAD), 132u};
    ui_draw_panel(ctx, hero, &card);

    int32_t temp_dC = m->ctrl_temp_dC;
    if (temp_dC < 0)
        temp_dC = 0;
    /* Simple 0..100.0C gauge mapping (0..1000 dC). */
    uint32_t t_clamp = (uint32_t)temp_dC;
    if (t_clamp > 1000u)
        t_clamp = 1000u;
    uint16_t pct = (uint16_t)((t_clamp * 100u + 500u) / 1000u);

    uint16_t tcol = accent;
    if (pct >= 85u)
        tcol = danger;
    else if (pct >= 70u)
        tcol = warn;

    ui_rect_t clip = inset_rect(hero, 6u);
    int16_t cx = (int16_t)(hero.x + 62u);
    int16_t cy = (int16_t)(hero.y + 72u);
    uint16_t outer_r = 52u;
    uint16_t thick = 10u;
    uint16_t sweep = 300u;
    uint16_t active_sweep = (uint16_t)((uint32_t)sweep * pct / 100u);
    uint16_t inactive = rgb565_lerp(card_fill, muted, 64u);
    ui_draw_ring_gauge(ctx, clip, cx, cy, outer_r, thick, 210, sweep, active_sweep,
                       rgb565_lerp(card_fill, tcol, 220u), inactive, card_fill);

    /* Temperature readout (center) */
    {
        char buf[16];
        fmt_d10(buf, sizeof(buf), (int32_t)m->ctrl_temp_dC);
        uint16_t tw = txt_w_est(buf);
        uint16_t tx = (tw < 96u) ? (uint16_t)((int)cx - (int)tw / 2) : hero.x;
        ui_draw_text(ctx, tx, (uint16_t)(hero.y + 60u), buf, tcol, card_fill);
        ui_draw_text(ctx, (uint16_t)(tx + tw + 4u), (uint16_t)(hero.y + 60u), "C", muted, card_fill);
    }

    /* Right-side details */
    ui_rect_t stat = {(uint16_t)(hero.x + 120u), (uint16_t)(hero.y + 18u), (uint16_t)(hero.w - 132u), 92u};
    ui_draw_round_rect(ctx, stat, panel, 10u);
    ui_draw_text(ctx, (uint16_t)(stat.x + 10u), (uint16_t)(stat.y + 8u), "STATE", muted, panel);
    ui_draw_value(ctx, (uint16_t)(stat.x + 10u), (uint16_t)(stat.y + 22u), "", (int32_t)m->thermal_state, text, panel);
    ui_draw_text(ctx, (uint16_t)(stat.x + 10u), (uint16_t)(stat.y + 40u), "LIMIT", muted, panel);
    const char *lim = "OK";
    if (m->limit_reason == LIMIT_REASON_LUG)
        lim = "LUG";
    else if (m->limit_reason == LIMIT_REASON_THERM)
        lim = "THERM";
    else if (m->limit_reason == LIMIT_REASON_SAG)
        lim = "SAG";
    ui_draw_text(ctx, (uint16_t)(stat.x + 10u), (uint16_t)(stat.y + 56u), lim, (m->limit_reason == LIMIT_REASON_USER) ? accent : warn, panel);
    ui_draw_rect(ctx, (ui_rect_t){(uint16_t)(stat.x + 8u), (uint16_t)(stat.y + 36u), (uint16_t)(stat.w - 16u), 1u}, stroke);

    ui_rect_t bottom = {PAD, (uint16_t)(hero.y + hero.h + G), (uint16_t)(DISP_W - 2u * PAD), 58u};
    ui_draw_panel(ctx, bottom, &card);
    ui_draw_text(ctx, (uint16_t)(bottom.x + 12u), (uint16_t)(bottom.y + 12u), "DERATE", muted, card_fill);
    ui_draw_value(ctx, (uint16_t)(bottom.x + 12u), (uint16_t)(bottom.y + 30u), "REASON", m->limit_reason, (m->limit_reason == LIMIT_REASON_USER) ? text : warn, card_fill);
}

static void render_diagnostics(ui_render_ctx_t *ctx, const ui_model_t *m,
                               uint16_t dist_d10, uint16_t wh_d10)
{
    (void)dist_d10;
    (void)wh_d10;
    const uint16_t bgc = ui_color(ctx, UI_COLOR_BG);
    const uint16_t panel = ui_color(ctx, UI_COLOR_PANEL);
    const uint16_t text = ui_color(ctx, UI_COLOR_TEXT);
    const uint16_t muted = ui_color(ctx, UI_COLOR_MUTED);
    const uint16_t shadow = rgb565_dim(panel);
    const uint16_t card_fill = rgb565_lerp(bgc, panel, 32u);
    const uint16_t stroke = rgb565_dim(muted);

    ui_draw_rect(ctx, (ui_rect_t){0, 0, DISP_W, DISP_H}, bgc);
    render_header_icon(ctx, "DIAG", UI_ICON_INFO);

    ui_panel_style_t card = {
        .radius = 10u,
        .border_thick = 1u,
        .shadow_dx = 2,
        .shadow_dy = 2,
        .fill = card_fill,
        .border = panel,
        .shadow = shadow,
        .flags = panel_flags_for_theme(m->theme),
    };

    uint16_t y = (uint16_t)(TOP_Y + TOP_H + G);
    ui_rect_t box = {PAD, y, (uint16_t)(DISP_W - 2u * PAD), 212u};
    ui_draw_panel(ctx, box, &card);
    ui_draw_text(ctx, (uint16_t)(box.x + 12u), (uint16_t)(box.y + 10u), "SENSORS", muted, card_fill);
    ui_draw_rect(ctx, (ui_rect_t){(uint16_t)(box.x + 12u), (uint16_t)(box.y + 28u), (uint16_t)(box.w - 24u), 1u}, stroke);

    struct rowdef { const char *label; int32_t v; };
    struct rowdef rows[] = {
        {"SPD dMPH", (int32_t)m->speed_dmph},
        {"RPM", (int32_t)m->rpm},
        {"CAD", (int32_t)m->cadence_rpm},
        {"THR %", (int32_t)m->throttle_pct},
        {"BRAKE", (int32_t)m->brake},
        {"ERR", (int32_t)m->err},
    };
    uint16_t ry = (uint16_t)(box.y + 40u);
    for (uint8_t i = 0; i < (uint8_t)(sizeof(rows) / sizeof(rows[0])); ++i)
    {
        ui_draw_text(ctx, (uint16_t)(box.x + 12u), (uint16_t)(ry + 2u), rows[i].label, text, card_fill);
        ui_draw_value(ctx, (uint16_t)(box.x + box.w - 72u), (uint16_t)(ry + 2u), "", rows[i].v, text, card_fill);
        ry = (uint16_t)(ry + 22u);
        if (i + 1u < (uint8_t)(sizeof(rows) / sizeof(rows[0])))
            ui_draw_rect(ctx, (ui_rect_t){(uint16_t)(box.x + 12u), (uint16_t)(ry - 4u), (uint16_t)(box.w - 24u), 1u}, stroke);
    }
}

static void render_bus(ui_render_ctx_t *ctx, const ui_model_t *m,
                       uint16_t dist_d10, uint16_t wh_d10)
{
    (void)dist_d10;
    (void)wh_d10;
    const uint16_t bgc = ui_color(ctx, UI_COLOR_BG);
    const uint16_t panel = ui_color(ctx, UI_COLOR_PANEL);
    const uint16_t text = ui_color(ctx, UI_COLOR_TEXT);
    const uint16_t muted = ui_color(ctx, UI_COLOR_MUTED);
    const uint16_t accent = ui_color(ctx, UI_COLOR_ACCENT);
    const uint16_t shadow = rgb565_dim(panel);
    const uint16_t card_fill = rgb565_lerp(bgc, panel, 32u);
    const uint16_t stroke = rgb565_dim(muted);

    ui_draw_rect(ctx, (ui_rect_t){0, 0, DISP_W, DISP_H}, bgc);
    render_header_icon(ctx, "BUS", UI_ICON_BUS);

    ui_panel_style_t card = {
        .radius = 10u,
        .border_thick = 1u,
        .shadow_dx = 2,
        .shadow_dy = 2,
        .fill = card_fill,
        .border = panel,
        .shadow = shadow,
        .flags = panel_flags_for_theme(m->theme),
    };

    uint16_t y = (uint16_t)(TOP_Y + TOP_H + G);
    ui_rect_t top = {PAD, y, (uint16_t)(DISP_W - 2u * PAD), 96u};
    ui_draw_panel(ctx, top, &card);
    ui_draw_text(ctx, (uint16_t)(top.x + 12u), (uint16_t)(top.y + 10u), "FRAMES", muted, card_fill);
    ui_draw_value(ctx, (uint16_t)(top.x + 92u), (uint16_t)(top.y + 10u), "CNT", m->bus_count, text, card_fill);
    ui_draw_rect(ctx, (ui_rect_t){(uint16_t)(top.x + 12u), (uint16_t)(top.y + 30u), (uint16_t)(top.w - 24u), 1u}, stroke);

    /* Last frame summary */
    ui_draw_value(ctx, (uint16_t)(top.x + 12u), (uint16_t)(top.y + 38u), "ID", m->bus_last_id, text, card_fill);
    ui_draw_value(ctx, (uint16_t)(top.x + 60u), (uint16_t)(top.y + 38u), "OP", m->bus_last_opcode, text, card_fill);
    ui_draw_value(ctx, (uint16_t)(top.x + 112u), (uint16_t)(top.y + 38u), "LEN", m->bus_last_len, text, card_fill);
    ui_draw_value(ctx, (uint16_t)(top.x + 164u), (uint16_t)(top.y + 38u), "DT", m->bus_last_dt_ms, muted, card_fill);

    /* Filter chips */
    ui_rect_t chip = {(uint16_t)(top.x + 12u), (uint16_t)(top.y + 66u), 52u, 20u};
    uint16_t chip_on = rgb565_lerp(panel, accent, 180u);
    uint16_t chip_off = panel;
    uint16_t chip_fg_on = bgc;
    uint16_t chip_fg_off = text;

    ui_draw_round_rect(ctx, chip, m->bus_diff ? chip_on : chip_off, 10u);
    ui_draw_text(ctx, (uint16_t)(chip.x + 10u), (uint16_t)(chip.y + 6u), m->bus_diff ? "DIFF" : "RAW", m->bus_diff ? chip_fg_on : chip_fg_off, m->bus_diff ? chip_on : chip_off);

    chip.x = (uint16_t)(chip.x + 60u);
    ui_draw_round_rect(ctx, chip, m->bus_changed_only ? chip_on : chip_off, 10u);
    ui_draw_text(ctx, (uint16_t)(chip.x + 10u), (uint16_t)(chip.y + 6u), m->bus_changed_only ? "CHG" : "ALL", m->bus_changed_only ? chip_fg_on : chip_fg_off, m->bus_changed_only ? chip_on : chip_off);

    chip.x = (uint16_t)(chip.x + 60u);
    ui_draw_round_rect(ctx, chip, m->bus_filter_id_active ? chip_on : chip_off, 10u);
    if (m->bus_filter_id_active)
        ui_draw_value(ctx, (uint16_t)(chip.x + 8u), (uint16_t)(chip.y + 6u), "ID", m->bus_filter_id, chip_fg_on, chip_on);
    else
        ui_draw_text(ctx, (uint16_t)(chip.x + 18u), (uint16_t)(chip.y + 6u), "ID", muted, chip_off);

    chip.x = (uint16_t)(chip.x + 60u);
    ui_draw_round_rect(ctx, chip, m->bus_filter_opcode_active ? chip_on : chip_off, 10u);
    if (m->bus_filter_opcode_active)
        ui_draw_value(ctx, (uint16_t)(chip.x + 6u), (uint16_t)(chip.y + 6u), "OP", m->bus_filter_opcode, chip_fg_on, chip_on);
    else
        ui_draw_text(ctx, (uint16_t)(chip.x + 14u), (uint16_t)(chip.y + 6u), "OP", muted, chip_off);

    ui_rect_t list = {PAD, (uint16_t)(top.y + top.h + G), (uint16_t)(DISP_W - 2u * PAD), 132u};
    ui_draw_panel(ctx, list, &card);
    ui_draw_text(ctx, (uint16_t)(list.x + 12u), (uint16_t)(list.y + 10u), "LATEST", muted, card_fill);
    ui_draw_rect(ctx, (ui_rect_t){(uint16_t)(list.x + 12u), (uint16_t)(list.y + 28u), (uint16_t)(list.w - 24u), 1u}, stroke);

    uint16_t ry = (uint16_t)(list.y + 38u);
    for (uint8_t i = 0; i < m->bus_entries && i < 6u; ++i)
    {
        ui_rect_t row = {(uint16_t)(list.x + 10u), ry, (uint16_t)(list.w - 20u), 16u};
        ui_draw_rect(ctx, row, card_fill);
        ui_draw_value(ctx, (uint16_t)(row.x + 0u), (uint16_t)(row.y + 2u), "ID", m->bus_list_id[i], text, card_fill);
        ui_draw_value(ctx, (uint16_t)(row.x + 48u), (uint16_t)(row.y + 2u), "OP", m->bus_list_op[i], text, card_fill);
        ui_draw_value(ctx, (uint16_t)(row.x + 104u), (uint16_t)(row.y + 2u), "L", m->bus_list_len[i], text, card_fill);
        ui_draw_value(ctx, (uint16_t)(row.x + 136u), (uint16_t)(row.y + 2u), "DT", m->bus_list_dt_ms[i], muted, card_fill);
        ry = (uint16_t)(ry + 18u);
        if (i + 1u < m->bus_entries)
            ui_draw_rect(ctx, (ui_rect_t){(uint16_t)(row.x + 0u), (uint16_t)(ry - 2u), (uint16_t)(row.w - 0u), 1u}, stroke);
    }
}

static void render_capture(ui_render_ctx_t *ctx, const ui_model_t *m,
                           uint16_t dist_d10, uint16_t wh_d10)
{
    (void)dist_d10;
    (void)wh_d10;
    const uint16_t bgc = ui_color(ctx, UI_COLOR_BG);
    const uint16_t panel = ui_color(ctx, UI_COLOR_PANEL);
    const uint16_t text = ui_color(ctx, UI_COLOR_TEXT);
    const uint16_t muted = ui_color(ctx, UI_COLOR_MUTED);
    const uint16_t accent = ui_color(ctx, UI_COLOR_ACCENT);
    const uint16_t shadow = rgb565_dim(panel);
    const uint16_t card_fill = rgb565_lerp(bgc, panel, 32u);
    const uint16_t stroke = rgb565_dim(muted);

    ui_draw_rect(ctx, (ui_rect_t){0, 0, DISP_W, DISP_H}, bgc);
    render_header_icon(ctx, "CAPTURE", UI_ICON_CAPTURE);

    ui_panel_style_t card = {
        .radius = 10u,
        .border_thick = 1u,
        .shadow_dx = 2,
        .shadow_dy = 2,
        .fill = card_fill,
        .border = panel,
        .shadow = shadow,
        .flags = panel_flags_for_theme(m->theme),
    };

    uint16_t y = (uint16_t)(TOP_Y + TOP_H + G);
    ui_rect_t hero = {PAD, y, (uint16_t)(DISP_W - 2u * PAD), 92u};
    ui_draw_panel(ctx, hero, &card);
    ui_draw_text(ctx, (uint16_t)(hero.x + 12u), (uint16_t)(hero.y + 10u), "STATUS", muted, card_fill);

    ui_rect_t btn = {(uint16_t)(hero.x + 12u), (uint16_t)(hero.y + 34u), (uint16_t)(hero.w - 24u), 44u};
    uint16_t btn_fill = m->capture_enabled ? rgb565_lerp(panel, accent, 180u) : panel;
    uint16_t btn_text = m->capture_enabled ? bgc : text;
    ui_draw_round_rect(ctx, btn, btn_fill, 12u);
    ui_draw_text(ctx, (uint16_t)(btn.x + 14u), (uint16_t)(btn.y + 14u), m->capture_enabled ? "STOP CAPTURE" : "START CAPTURE", btn_text, btn_fill);

    ui_rect_t stat = {PAD, (uint16_t)(hero.y + hero.h + G), (uint16_t)(DISP_W - 2u * PAD), 70u};
    ui_draw_panel(ctx, stat, &card);
    ui_draw_text(ctx, (uint16_t)(stat.x + 12u), (uint16_t)(stat.y + 10u), "FRAMES", muted, card_fill);
    ui_draw_rect(ctx, (ui_rect_t){(uint16_t)(stat.x + 12u), (uint16_t)(stat.y + 28u), (uint16_t)(stat.w - 24u), 1u}, stroke);
    ui_draw_value(ctx, (uint16_t)(stat.x + 12u), (uint16_t)(stat.y + 36u), "COUNT", (int32_t)m->capture_count, text, card_fill);
}

static void render_alerts(ui_render_ctx_t *ctx, const ui_model_t *m,
                          uint16_t dist_d10, uint16_t wh_d10)
{
    (void)dist_d10;
    (void)wh_d10;
    const uint16_t bgc = ui_color(ctx, UI_COLOR_BG);
    const uint16_t panel = ui_color(ctx, UI_COLOR_PANEL);
    const uint16_t text = ui_color(ctx, UI_COLOR_TEXT);
    const uint16_t muted = ui_color(ctx, UI_COLOR_MUTED);
    const uint16_t accent = ui_color(ctx, UI_COLOR_ACCENT);
    const uint16_t warn = ui_color(ctx, UI_COLOR_WARN);
    const uint16_t danger = ui_color(ctx, UI_COLOR_DANGER);
    const uint16_t shadow = rgb565_dim(panel);
    const uint16_t card_fill = rgb565_lerp(bgc, panel, 32u);
    const uint16_t stroke = rgb565_dim(muted);

    ui_draw_rect(ctx, (ui_rect_t){0, 0, DISP_W, DISP_H}, bgc);
    render_header_icon(ctx, "ALERTS", UI_ICON_ALERT);

    ui_panel_style_t card = {
        .radius = 10u,
        .border_thick = 1u,
        .shadow_dx = 2,
        .shadow_dy = 2,
        .fill = card_fill,
        .border = panel,
        .shadow = shadow,
        .flags = panel_flags_for_theme(m->theme),
    };

    uint16_t y = (uint16_t)(TOP_Y + TOP_H + G);
    ui_rect_t summary = {PAD, y, (uint16_t)(DISP_W - 2u * PAD), 92u};
    ui_draw_panel(ctx, summary, &card);
    ui_draw_text(ctx, (uint16_t)(summary.x + 12u), (uint16_t)(summary.y + 10u), "STATUS", muted, card_fill);
    ui_draw_rect(ctx, (ui_rect_t){(uint16_t)(summary.x + 12u), (uint16_t)(summary.y + 28u), (uint16_t)(summary.w - 24u), 1u}, stroke);

    ui_rect_t chip = {(uint16_t)(summary.x + 12u), (uint16_t)(summary.y + 38u), 72u, 22u};
    uint16_t err_fill = m->err ? rgb565_lerp(panel, danger, 200u) : panel;
    ui_draw_round_rect(ctx, chip, err_fill, 10u);
    ui_draw_text(ctx, (uint16_t)(chip.x + 10u), (uint16_t)(chip.y + 6u), m->err ? "ERROR" : "OK", m->err ? bgc : text, err_fill);

    chip.x = (uint16_t)(chip.x + 82u);
    uint16_t lim_fill = (m->limit_reason != LIMIT_REASON_USER) ? rgb565_lerp(panel, warn, 190u) : panel;
    ui_draw_round_rect(ctx, chip, lim_fill, 10u);
    ui_draw_text(ctx, (uint16_t)(chip.x + 10u), (uint16_t)(chip.y + 6u),
                 (m->limit_reason != LIMIT_REASON_USER) ? "LIMIT" : "CLEAR",
                 (m->limit_reason != LIMIT_REASON_USER) ? bgc : text, lim_fill);

    chip.x = (uint16_t)(chip.x + 82u);
    ui_draw_round_rect(ctx, chip, panel, 10u);
    ui_draw_value(ctx, (uint16_t)(chip.x + 8u), (uint16_t)(chip.y + 6u), "CNT", (int32_t)m->alert_count, text, panel);

    ui_rect_t ack = {(uint16_t)(summary.x + 12u), (uint16_t)(summary.y + 64u), 84u, 22u};
    uint16_t ack_fill = m->alert_ack_active ? rgb565_lerp(panel, muted, 120u) : panel;
    ui_draw_round_rect(ctx, ack, ack_fill, 10u);
    ui_draw_text(ctx, (uint16_t)(ack.x + 10u), (uint16_t)(ack.y + 6u), m->alert_ack_active ? "ACKED" : "UNACK", text, ack_fill);

    ui_rect_t warn_chip = {(uint16_t)(ack.x + ack.w + 12u), (uint16_t)(ack.y), 96u, 22u};
    uint8_t warn_active = (m->err || m->limit_reason != LIMIT_REASON_USER) ? 1u : 0u;
    uint16_t warn_fill = warn_active ? rgb565_lerp(panel, warn, 170u) : panel;
    uint16_t warn_text = warn_active ? bgc : text;
    const char *warn_label = warn_active ? "WARN" : "CLEAR";
    if (m->alert_ack_active && warn_active)
    {
        warn_fill = rgb565_lerp(panel, muted, 120u);
        warn_text = text;
        warn_label = "ACK";
    }
    ui_draw_round_rect(ctx, warn_chip, warn_fill, 10u);
    ui_draw_text(ctx, (uint16_t)(warn_chip.x + 10u), (uint16_t)(warn_chip.y + 6u),
                 warn_label, warn_text, warn_fill);

    ui_rect_t list = {PAD, (uint16_t)(summary.y + summary.h + G), (uint16_t)(DISP_W - 2u * PAD), 168u};
    ui_draw_panel(ctx, list, &card);
    ui_draw_text(ctx, (uint16_t)(list.x + 12u), (uint16_t)(list.y + 10u), "LATEST", muted, card_fill);
    ui_draw_rect(ctx, (ui_rect_t){(uint16_t)(list.x + 12u), (uint16_t)(list.y + 28u), (uint16_t)(list.w - 24u), 1u}, stroke);

    uint16_t ry = (uint16_t)(list.y + 38u);
    for (uint8_t i = 0; i < m->alert_entries && i < 3u; ++i)
    {
        uint8_t acked = (m->alert_ack_mask & (uint8_t)(1u << i)) ? 1u : 0u;
        uint8_t sel = (m->alert_selected == i) ? 1u : 0u;
        uint16_t row_fill = sel ? rgb565_lerp(card_fill, accent, 28u) : card_fill;
        uint16_t row_text = acked ? muted : text;
        if (sel)
            row_text = bgc;

        ui_rect_t row = {(uint16_t)(list.x + 8u), ry, (uint16_t)(list.w - 16u), 38u};
        ui_draw_round_rect(ctx, row, row_fill, 10u);
        if (sel)
            ui_draw_rect(ctx, (ui_rect_t){(uint16_t)(row.x + 2u), (uint16_t)(row.y + 6u), 4u, 22u}, accent);

        uint16_t icon_fg = row_text;
#if defined(UI_PIXEL_SIM) || UI_LCD_HW
        ui_icon_id_t icon = alert_type_icon(m->alert_type[i]);
        ui_draw_icon(ctx, (uint16_t)(row.x + 10u), (uint16_t)(row.y + 10u), icon, icon_fg, row_fill);
#else
        ui_draw_warning_icon(ctx, (uint16_t)(row.x + 12u), (uint16_t)(row.y + 12u), icon_fg);
#endif

        const char *etype = alert_type_label(m->alert_type[i]);
        ui_draw_text(ctx, (uint16_t)(row.x + 32u), (uint16_t)(row.y + 8u), etype, row_text, row_fill);
        ui_draw_value(ctx, (uint16_t)(row.x + 112u), (uint16_t)(row.y + 8u), "F", (int32_t)m->alert_flags[i], row_text, row_fill);

        char age_buf[12];
        char dist_buf[12];
        fmt_seconds_label(age_buf, sizeof(age_buf), m->alert_age_s[i]);
        fmt_distance_label(dist_buf, sizeof(dist_buf), m->alert_dist_d10[i], m->units);
        ui_draw_text(ctx, (uint16_t)(row.x + 32u), (uint16_t)(row.y + 22u), age_buf, muted, row_fill);
        ui_draw_text(ctx, (uint16_t)(row.x + row.w - 52u), (uint16_t)(row.y + 22u), dist_buf, muted, row_fill);

        ry = (uint16_t)(ry + 44u);
    }
}

static void render_tune(ui_render_ctx_t *ctx, const ui_model_t *m,
                        uint16_t dist_d10, uint16_t wh_d10)
{
    (void)dist_d10;
    (void)wh_d10;
    const uint16_t bgc = ui_color(ctx, UI_COLOR_BG);
    const uint16_t panel = ui_color(ctx, UI_COLOR_PANEL);
    const uint16_t text = ui_color(ctx, UI_COLOR_TEXT);
    const uint16_t muted = ui_color(ctx, UI_COLOR_MUTED);
    const uint16_t accent = ui_color(ctx, UI_COLOR_ACCENT);
    const uint16_t shadow = rgb565_dim(panel);
    const uint16_t card_fill = rgb565_lerp(bgc, panel, 32u);
    const uint16_t stroke = rgb565_dim(muted);

    ui_draw_rect(ctx, (ui_rect_t){0, 0, DISP_W, DISP_H}, bgc);
    render_header_icon(ctx, "TUNE", UI_ICON_TUNE);

    ui_panel_style_t card = {
        .radius = 12u,
        .border_thick = 1u,
        .shadow_dx = 2,
        .shadow_dy = 2,
        .fill = card_fill,
        .border = panel,
        .shadow = shadow,
        .flags = panel_flags_for_theme(m->theme),
    };
    ui_panel_style_t card_sel = card;
    card_sel.border = accent;
    card_sel.fill = rgb565_lerp(card_fill, accent, 24u);

    uint16_t gap = 10u;
    uint16_t w = (uint16_t)((DISP_W - 2u * PAD - gap) / 2u);
    uint16_t y = (uint16_t)(TOP_Y + TOP_H + G);
    ui_rect_t c0 = {PAD, y, w, 70u};
    ui_rect_t c1 = {(uint16_t)(PAD + w + gap), y, w, 70u};
    ui_rect_t c2 = {(uint16_t)(PAD + (DISP_W - 2u * PAD - w) / 2u), (uint16_t)(y + 80u), w, 70u};

    const ui_panel_style_t *s0 = (m->tune_index == 0u) ? &card_sel : &card;
    const ui_panel_style_t *s1 = (m->tune_index == 1u) ? &card_sel : &card;
    const ui_panel_style_t *s2 = (m->tune_index == 2u) ? &card_sel : &card;

    ui_draw_panel(ctx, c0, s0);
    ui_draw_text(ctx, (uint16_t)(c0.x + 12u), (uint16_t)(c0.y + 12u), "CURRENT", muted, s0->fill);
    ui_draw_rect(ctx, (ui_rect_t){(uint16_t)(c0.x + 12u), (uint16_t)(c0.y + 32u), (uint16_t)(c0.w - 24u), 1u}, stroke);
    ui_draw_value(ctx, (uint16_t)(c0.x + 12u), (uint16_t)(c0.y + 40u), "dA", m->tune_cap_current_dA, text, s0->fill);

    ui_draw_panel(ctx, c1, s1);
    ui_draw_text(ctx, (uint16_t)(c1.x + 12u), (uint16_t)(c1.y + 12u), "RAMP", muted, s1->fill);
    ui_draw_rect(ctx, (ui_rect_t){(uint16_t)(c1.x + 12u), (uint16_t)(c1.y + 32u), (uint16_t)(c1.w - 24u), 1u}, stroke);
    ui_draw_value(ctx, (uint16_t)(c1.x + 12u), (uint16_t)(c1.y + 40u), "W/s", m->tune_ramp_wps, text, s1->fill);

    ui_draw_panel(ctx, c2, s2);
    ui_draw_text(ctx, (uint16_t)(c2.x + 12u), (uint16_t)(c2.y + 12u), "BOOST", muted, s2->fill);
    ui_draw_rect(ctx, (ui_rect_t){(uint16_t)(c2.x + 12u), (uint16_t)(c2.y + 32u), (uint16_t)(c2.w - 24u), 1u}, stroke);
    ui_draw_value(ctx, (uint16_t)(c2.x + 12u), (uint16_t)(c2.y + 40u), "s", m->tune_boost_s, text, s2->fill);
}

static void render_ambient(ui_render_ctx_t *ctx, const ui_model_t *m,
                           uint16_t dist_d10, uint16_t wh_d10)
{
    (void)dist_d10;
    (void)wh_d10;
    const uint16_t bgc = ui_color(ctx, UI_COLOR_BG);
    const uint16_t panel = ui_color(ctx, UI_COLOR_PANEL);
    const uint16_t text = ui_color(ctx, UI_COLOR_TEXT);
    const uint16_t muted = ui_color(ctx, UI_COLOR_MUTED);
    const uint16_t accent = ui_color(ctx, UI_COLOR_ACCENT);
    const uint16_t shadow = rgb565_dim(panel);
    const uint16_t card_fill = rgb565_lerp(bgc, panel, 32u);
    const uint16_t stroke = rgb565_dim(muted);

    ui_draw_rect(ctx, (ui_rect_t){0, 0, DISP_W, DISP_H}, bgc);
    render_header_icon(ctx, "CHARGE", UI_ICON_BATTERY);

    ui_panel_style_t card = {
        .radius = 10u,
        .border_thick = 1u,
        .shadow_dx = 2,
        .shadow_dy = 2,
        .fill = card_fill,
        .border = panel,
        .shadow = shadow,
        .flags = panel_flags_for_theme(m->theme),
    };

    uint16_t y = (uint16_t)(TOP_Y + TOP_H + G);
    ui_rect_t hero = {PAD, y, (uint16_t)(DISP_W - 2u * PAD), 148u};
    ui_draw_panel(ctx, hero, &card);

    uint8_t soc = m->soc_pct;
    if (soc > 100u)
        soc = 100u;
    uint16_t inactive = rgb565_lerp(card_fill, muted, 64u);
    ui_rect_t clip = inset_rect(hero, 6u);
    int16_t cx = (int16_t)(hero.x + hero.w / 2u);
    int16_t cy = (int16_t)(hero.y + 82u);
    uint16_t outer_r = 60u;
    uint16_t thick = 10u;
    uint16_t sweep = 360u;
    uint16_t active_sweep = (uint16_t)((uint32_t)sweep * soc / 100u);
    ui_draw_ring_gauge(ctx, clip, cx, cy, outer_r, thick, -90, sweep, active_sweep,
                       rgb565_lerp(card_fill, accent, 220u), inactive, card_fill);

    ui_draw_text(ctx, (uint16_t)(hero.x + 12u), (uint16_t)(hero.y + 10u), "SOC", muted, card_fill);
    ui_draw_value(ctx, (uint16_t)(hero.x + 12u), (uint16_t)(hero.y + 28u), "", soc, text, card_fill);

    ui_draw_rect(ctx, (ui_rect_t){(uint16_t)(hero.x + 12u), (uint16_t)(hero.y + 56u), (uint16_t)(hero.w - 24u), 1u}, stroke);
    ui_draw_text(ctx, (uint16_t)(hero.x + 12u), (uint16_t)(hero.y + 64u), "CUR", muted, card_fill);
    ui_draw_value(ctx, (uint16_t)(hero.x + 12u), (uint16_t)(hero.y + 82u), "dA", m->batt_dA, text, card_fill);
}

static void render_about(ui_render_ctx_t *ctx, const ui_model_t *m,
                         uint16_t dist_d10, uint16_t wh_d10)
{
    (void)dist_d10;
    (void)wh_d10;
    const uint16_t bgc = ui_color(ctx, UI_COLOR_BG);
    const uint16_t panel = ui_color(ctx, UI_COLOR_PANEL);
    const uint16_t text = ui_color(ctx, UI_COLOR_TEXT);
    const uint16_t muted = ui_color(ctx, UI_COLOR_MUTED);
    const uint16_t accent = ui_color(ctx, UI_COLOR_ACCENT);
    const uint16_t shadow = rgb565_dim(panel);
    const uint16_t card_fill = rgb565_lerp(bgc, panel, 32u);
    const uint16_t stroke = rgb565_dim(muted);

    ui_draw_rect(ctx, (ui_rect_t){0, 0, DISP_W, DISP_H}, bgc);
    render_header_icon(ctx, "ABOUT", UI_ICON_INFO);

    ui_panel_style_t card = {
        .radius = 10u,
        .border_thick = 1u,
        .shadow_dx = 2,
        .shadow_dy = 2,
        .fill = card_fill,
        .border = panel,
        .shadow = shadow,
        .flags = panel_flags_for_theme(m->theme),
    };

    uint16_t y = (uint16_t)(TOP_Y + TOP_H + G);
    ui_rect_t info = {PAD, y, (uint16_t)(DISP_W - 2u * PAD), 120u};
    ui_draw_panel(ctx, info, &card);
    ui_draw_text(ctx, (uint16_t)(info.x + 12u), (uint16_t)(info.y + 10u), "FIRMWARE", muted, card_fill);
    ui_draw_rect(ctx, (ui_rect_t){(uint16_t)(info.x + 12u), (uint16_t)(info.y + 28u), (uint16_t)(info.w - 24u), 1u}, stroke);

    ui_draw_text(ctx, (uint16_t)(info.x + 12u), (uint16_t)(info.y + 38u), "FW", muted, card_fill);
    ui_draw_text(ctx, (uint16_t)(info.x + 60u), (uint16_t)(info.y + 38u), "OPEN-BC280", text, card_fill);
    ui_draw_text(ctx, (uint16_t)(info.x + 12u), (uint16_t)(info.y + 58u), "BUILD", muted, card_fill);
    ui_draw_text(ctx, (uint16_t)(info.x + 60u), (uint16_t)(info.y + 58u), "DEV", text, card_fill);
    ui_draw_text(ctx, (uint16_t)(info.x + 12u), (uint16_t)(info.y + 78u), "HW", muted, card_fill);
    ui_draw_text(ctx, (uint16_t)(info.x + 60u), (uint16_t)(info.y + 78u), "BC280", text, card_fill);
    ui_draw_text(ctx, (uint16_t)(info.x + 12u), (uint16_t)(info.y + 98u), "BOOT", muted, card_fill);
    ui_draw_text(ctx, (uint16_t)(info.x + 60u), (uint16_t)(info.y + 98u), "REBOOT", accent, card_fill);

    ui_rect_t row = {PAD, (uint16_t)(info.y + info.h + G), (uint16_t)(DISP_W - 2u * PAD), 56u};
    ui_draw_panel(ctx, row, &card);
    ui_draw_text(ctx, (uint16_t)(row.x + 12u), (uint16_t)(row.y + 10u), "STATUS", muted, card_fill);

#if defined(UI_PIXEL_SIM) || UI_LCD_HW
    uint16_t icon_y = (uint16_t)(row.y + 26u);
    ui_draw_icon(ctx, (uint16_t)(row.x + 12u), icon_y, UI_ICON_BLE, accent, card_fill);
    ui_draw_text(ctx, (uint16_t)(row.x + 34u), (uint16_t)(icon_y + 4u), "BLE", text, card_fill);

    ui_draw_icon(ctx, (uint16_t)(row.x + 86u), icon_y, UI_ICON_LOCK, accent, card_fill);
    ui_draw_text(ctx, (uint16_t)(row.x + 108u), (uint16_t)(icon_y + 4u), "LOCK", text, card_fill);

    ui_draw_icon(ctx, (uint16_t)(row.x + 166u), icon_y, UI_ICON_THERMO, accent, card_fill);
    ui_draw_text(ctx, (uint16_t)(row.x + 188u), (uint16_t)(icon_y + 4u), "TEMP", text, card_fill);
#endif
}

static void render_header(ui_render_ctx_t *ctx, const char *title)
{
    ui_rect_t bar = {0, TOP_Y, DISP_W, TOP_H};
    uint16_t panel = ui_color(ctx, UI_COLOR_PANEL);
    uint16_t text = ui_color(ctx, UI_COLOR_TEXT);
    ui_draw_round_rect(ctx, bar, panel, 6u);
    ui_draw_text(ctx, (uint16_t)(bar.x + 8u), (uint16_t)(bar.y + 6u), title, text, panel);
}

static void render_header_icon(ui_render_ctx_t *ctx, const char *title, ui_icon_id_t icon)
{
    ui_rect_t bar = {0, TOP_Y, DISP_W, TOP_H};
    uint16_t panel = ui_color(ctx, UI_COLOR_PANEL);
    uint16_t text = ui_color(ctx, UI_COLOR_TEXT);
    ui_draw_round_rect(ctx, bar, panel, 6u);

    uint16_t title_x = (uint16_t)(bar.x + 8u);
#if defined(UI_PIXEL_SIM) || UI_LCD_HW
    if (icon != UI_ICON_NONE)
    {
        uint16_t iy = bar.y;
        if (bar.h > ICON_SIZE)
            iy = (uint16_t)(bar.y + (bar.h - ICON_SIZE) / 2u);
        ui_draw_icon(ctx, (uint16_t)(bar.x + 6u), iy, icon, text, panel);
        title_x = (uint16_t)(bar.x + 6u + ICON_SIZE + 6u);
    }
#else
    (void)icon;
#endif
    ui_draw_text(ctx, title_x, (uint16_t)(bar.y + 6u), title, text, panel);
}

static void render_table_header(ui_render_ctx_t *ctx, uint16_t y, const char *left, const char *right)
{
    ui_rect_t row = {PAD, y, (uint16_t)(DISP_W - 2u * PAD), 16u};
    uint16_t bg = ui_color(ctx, UI_COLOR_BG);
    uint16_t text = ui_color(ctx, UI_COLOR_TEXT);
    ui_draw_rect(ctx, row, bg);
    ui_draw_text(ctx, (uint16_t)(row.x + 4u), (uint16_t)(row.y + 2u), left, text, bg);
    ui_draw_text(ctx, (uint16_t)(row.x + row.w - 52u), (uint16_t)(row.y + 2u), right, text, bg);
}

static void render_table_row(ui_render_ctx_t *ctx, uint16_t y, const char *label, int32_t value)
{
    ui_rect_t row = {PAD, y, (uint16_t)(DISP_W - 2u * PAD), 18u};
    uint16_t bg = ui_color(ctx, UI_COLOR_BG);
    uint16_t text = ui_color(ctx, UI_COLOR_TEXT);
    ui_draw_rect(ctx, row, bg);
    ui_draw_text(ctx, (uint16_t)(row.x + 4u), (uint16_t)(row.y + 2u), label, text, bg);
    ui_draw_value(ctx, (uint16_t)(row.x + row.w - 64u), (uint16_t)(row.y + 2u), "", value, text, bg);
}

static void render_table_row_text(ui_render_ctx_t *ctx, uint16_t y, const char *label, const char *value)
{
    ui_rect_t row = {PAD, y, (uint16_t)(DISP_W - 2u * PAD), 18u};
    uint16_t bg = ui_color(ctx, UI_COLOR_BG);
    uint16_t text = ui_color(ctx, UI_COLOR_TEXT);
    ui_draw_rect(ctx, row, bg);
    ui_draw_text(ctx, (uint16_t)(row.x + 4u), (uint16_t)(row.y + 2u), label, text, bg);
    ui_draw_text(ctx, (uint16_t)(row.x + row.w - 64u), (uint16_t)(row.y + 2u), value ? value : "", text, bg);
}

static void render_table_row_hex(ui_render_ctx_t *ctx, uint16_t y, const char *label, uint32_t value)
{
    ui_rect_t row = {PAD, y, (uint16_t)(DISP_W - 2u * PAD), 18u};
    uint16_t bg = ui_color(ctx, UI_COLOR_BG);
    uint16_t text = ui_color(ctx, UI_COLOR_TEXT);
    ui_draw_rect(ctx, row, bg);
    ui_draw_text(ctx, (uint16_t)(row.x + 4u), (uint16_t)(row.y + 2u), label, text, bg);
    ui_draw_text(ctx, (uint16_t)(row.x + row.w - 76u), (uint16_t)(row.y + 2u), "0x", text, bg);
    char buf[12];
    fmt_u32_hex8(buf, sizeof(buf), value);
    ui_draw_text(ctx, (uint16_t)(row.x + row.w - 56u), (uint16_t)(row.y + 2u), buf, text, bg);
}

static void render_engineer_raw(ui_render_ctx_t *ctx, const ui_model_t *m,
                                uint16_t dist_d10, uint16_t wh_d10)
{
    (void)dist_d10;
    (void)wh_d10;
    ui_rect_t bg = {0, 0, DISP_W, DISP_H};
    ui_draw_rect(ctx, bg, ui_color(ctx, UI_COLOR_BG));
    render_header(ctx, "ENG RAW");
    render_table_header(ctx, (uint16_t)(TOP_Y + TOP_H + G), "SENSORS", "VAL");
    uint16_t y = (uint16_t)(TOP_Y + TOP_H + G + 20u);
    render_table_row(ctx, y, "SPD dMPH", m->speed_dmph); y += 18u;
    render_table_row(ctx, y, "RPM", m->rpm); y += 18u;
    render_table_row(ctx, y, "CAD", m->cadence_rpm); y += 18u;
    render_table_row(ctx, y, "TQ RAW", m->torque_raw); y += 18u;
    render_table_row(ctx, y, "THR %", m->throttle_pct); y += 18u;
    render_table_row(ctx, y, "BRAKE", m->brake); y += 18u;
    render_table_row_hex(ctx, y, "BTN", m->buttons); y += 18u;
    render_table_row(ctx, y, "SOC", m->soc_pct); y += 18u;
    render_table_row(ctx, y, "ERR", m->err);
}

static void render_engineer_power(ui_render_ctx_t *ctx, const ui_model_t *m,
                                  uint16_t dist_d10, uint16_t wh_d10)
{
    (void)dist_d10;
    (void)wh_d10;
    ui_rect_t bg = {0, 0, DISP_W, DISP_H};
    ui_draw_rect(ctx, bg, ui_color(ctx, UI_COLOR_BG));
    render_header(ctx, "ENG PWR");
    render_table_header(ctx, (uint16_t)(TOP_Y + TOP_H + G), "POWER", "VAL");
    uint16_t y = (uint16_t)(TOP_Y + TOP_H + G + 20u);
    render_table_row(ctx, y, "BATT dV", m->batt_dV); y += 18u;
    render_table_row(ctx, y, "BATT dA", m->batt_dA); y += 18u;
    render_table_row(ctx, y, "PHASE dA", m->phase_dA); y += 18u;
    render_table_row(ctx, y, "SAG dV", m->sag_margin_dV); y += 18u;
    render_table_row(ctx, y, "THERM", m->thermal_state); y += 18u;
    render_table_row(ctx, y, "TEMP dC", m->ctrl_temp_dC); y += 18u;
    render_table_row(ctx, y, "LIMIT W", m->limit_power_w); y += 18u;
    if (m->regen_supported)
        render_table_row(ctx, y, "REGEN W", m->regen_cmd_power_w);
    else
        render_table_row_text(ctx, y, "REGEN", "NA");
    y += 18u;
    render_table_row(ctx, y, "DERATE", m->limit_reason);
}

static void render_dashboard_partial(ui_render_ctx_t *ctx, const ui_model_t *m,
                                     uint16_t dist_d10, uint16_t wh_d10,
                                     const ui_dirty_t *dirty)
{
    if (!dirty || dirty->full)
    {
        render_dashboard(ctx, m, dist_d10, wh_d10);
        return;
    }

    ui_dash_v2_layout_t l = dash_v2_layout();
    const uint16_t bg = ui_color(ctx, UI_COLOR_BG);
    const uint16_t panel = ui_color(ctx, UI_COLOR_PANEL);
    const uint16_t text = ui_color(ctx, UI_COLOR_TEXT);
    const uint16_t muted = ui_color(ctx, UI_COLOR_MUTED);
    const uint16_t accent = ui_color(ctx, UI_COLOR_ACCENT);
    const uint16_t warn = ui_color(ctx, UI_COLOR_WARN);
    const uint16_t danger = ui_color(ctx, UI_COLOR_DANGER);
    const uint16_t ok = ui_color(ctx, UI_COLOR_OK);
    const uint16_t stroke = rgb565_dim(muted);
    const uint16_t card_fill = rgb565_lerp(bg, panel, 32u);

    if (rect_dirty(dirty, l.top_area))
        dash_v2_render_top(ctx, m, &l, bg, text, muted, card_fill, stroke, warn, danger, ok);

    if (rect_dirty(dirty, l.speed_in))
        dash_v2_render_speed_inner(ctx, m, &l, panel, text, muted, accent, warn, stroke, card_fill);

    if (rect_dirty(dirty, l.tray_in))
        dash_v2_render_tray_inner(ctx, m, &l, dist_d10, wh_d10, text, muted, stroke, card_fill);
}

static void dirty_graphs(ui_dirty_t *d, const ui_model_t *m, const ui_model_t *p)
{
    ui_graph_layout_t l = graph_layout();

    /* Always refresh the plot area to advance the strip chart. */
    ui_dirty_add(d, l.graph_dirty);

    if (m->graph_channel != p->graph_channel)
        ui_dirty_add(d, l.chip_channel);
    if (m->graph_window_s != p->graph_window_s)
        ui_dirty_add(d, l.chip_window);
    if (m->graph_sample_hz != p->graph_sample_hz)
        ui_dirty_add(d, l.chip_hz);
}

static void render_graphs_partial(ui_render_ctx_t *ctx, const ui_model_t *m,
                                  uint16_t dist_d10, uint16_t wh_d10,
                                  const ui_dirty_t *dirty)
{
    (void)dist_d10;
    (void)wh_d10;
    if (!dirty || dirty->full)
    {
        render_graphs(ctx, m, dist_d10, wh_d10);
        return;
    }

    ui_graph_layout_t l = graph_layout();
    const uint16_t bgc = ui_color(ctx, UI_COLOR_BG);
    const uint16_t panel = ui_color(ctx, UI_COLOR_PANEL);
    const uint16_t text = ui_color(ctx, UI_COLOR_TEXT);
    const uint16_t muted = ui_color(ctx, UI_COLOR_MUTED);
    const uint16_t accent = ui_color(ctx, UI_COLOR_ACCENT);
    const uint16_t stroke = rgb565_dim(muted);
    const uint16_t shadow = rgb565_dim(panel);
    const uint16_t card_fill = rgb565_lerp(bgc, panel, 32u);

    ui_panel_style_t card = {
        .radius = 10u,
        .border_thick = 1u,
        .shadow_dx = 2,
        .shadow_dy = 2,
        .fill = card_fill,
        .border = panel,
        .shadow = shadow,
    };

    if (rect_dirty(dirty, l.chip_channel))
        render_graph_channel_chip(ctx, m, &l, bgc, panel, accent);
    if (rect_dirty(dirty, l.chip_window))
        render_graph_window_chip(ctx, m, &l, text, panel);
    if (rect_dirty(dirty, l.chip_hz))
        render_graph_hz_chip(ctx, m, &l, text, panel);
    if (rect_dirty(dirty, l.graph_dirty))
        render_graph_panel(ctx, &l, &card, card_fill, stroke, accent, muted);
}

static const ui_screen_def_t k_ui_screens[] = {
    {
        .id = UI_PAGE_DASHBOARD,
        .flags = 0u,
        .name = "dashboard",
        .render_full = render_dashboard,
        .render_partial = render_dashboard_partial,
        .dirty_fn = dirty_dashboard_v2,
    },
    {
        .id = UI_PAGE_FOCUS,
        .flags = 0u,
        .name = "focus",
        .render_full = render_focus,
        .render_partial = NULL,
        .dirty_fn = NULL,
    },
    {
        .id = UI_PAGE_GRAPHS,
        .flags = 0u,
        .name = "graphs",
        .render_full = render_graphs,
        .render_partial = render_graphs_partial,
        .dirty_fn = dirty_graphs,
    },
    {
        .id = UI_PAGE_TRIP,
        .flags = 0u,
        .name = "trip",
        .render_full = render_trip_summary,
        .render_partial = NULL,
        .dirty_fn = dirty_trip_summary,
    },
    {
        .id = UI_PAGE_PROFILES,
        .flags = 0u,
        .name = "profiles",
        .render_full = render_profiles,
        .render_partial = NULL,
        .dirty_fn = NULL,
    },
    {
        .id = UI_PAGE_SETTINGS,
        .flags = 0u,
        .name = "settings",
        .render_full = render_settings,
        .render_partial = NULL,
        .dirty_fn = NULL,
    },
    {
        .id = UI_PAGE_CRUISE,
        .flags = 0u,
        .name = "cruise",
        .render_full = render_cruise,
        .render_partial = NULL,
        .dirty_fn = NULL,
    },
    {
        .id = UI_PAGE_BATTERY,
        .flags = 0u,
        .name = "battery",
        .render_full = render_battery_screen,
        .render_partial = NULL,
        .dirty_fn = NULL,
    },
    {
        .id = UI_PAGE_THERMAL,
        .flags = 0u,
        .name = "thermal",
        .render_full = render_thermal,
        .render_partial = NULL,
        .dirty_fn = NULL,
    },
    {
        .id = UI_PAGE_DIAGNOSTICS,
        .flags = 0u,
        .name = "diag",
        .render_full = render_diagnostics,
        .render_partial = NULL,
        .dirty_fn = NULL,
    },
    {
        .id = UI_PAGE_BUS,
        .flags = 0u,
        .name = "bus",
        .render_full = render_bus,
        .render_partial = NULL,
        .dirty_fn = NULL,
    },
    {
        .id = UI_PAGE_CAPTURE,
        .flags = 0u,
        .name = "capture",
        .render_full = render_capture,
        .render_partial = NULL,
        .dirty_fn = NULL,
    },
    {
        .id = UI_PAGE_ALERTS,
        .flags = 0u,
        .name = "alerts",
        .render_full = render_alerts,
        .render_partial = NULL,
        .dirty_fn = NULL,
    },
    {
        .id = UI_PAGE_TUNE,
        .flags = 0u,
        .name = "tune",
        .render_full = render_tune,
        .render_partial = NULL,
        .dirty_fn = NULL,
    },
    {
        .id = UI_PAGE_AMBIENT,
        .flags = 0u,
        .name = "ambient",
        .render_full = render_ambient,
        .render_partial = NULL,
        .dirty_fn = NULL,
    },
    {
        .id = UI_PAGE_ABOUT,
        .flags = 0u,
        .name = "about",
        .render_full = render_about,
        .render_partial = NULL,
        .dirty_fn = NULL,
    },
    {
        .id = UI_PAGE_ENGINEER_RAW,
        .flags = 0u,
        .name = "eng_raw",
        .render_full = render_engineer_raw,
        .render_partial = NULL,
        .dirty_fn = NULL,
    },
    {
        .id = UI_PAGE_ENGINEER_POWER,
        .flags = 0u,
        .name = "eng_power",
        .render_full = render_engineer_power,
        .render_partial = NULL,
        .dirty_fn = NULL,
    },
};

static const uint8_t k_ui_layout[] = {
    UI_PAGE_DASHBOARD,
    UI_PAGE_FOCUS,
    UI_PAGE_GRAPHS,
    UI_PAGE_TRIP,
    UI_PAGE_PROFILES,
    UI_PAGE_SETTINGS,
    UI_PAGE_CRUISE,
    UI_PAGE_BATTERY,
    UI_PAGE_THERMAL,
    UI_PAGE_DIAGNOSTICS,
    UI_PAGE_BUS,
    UI_PAGE_CAPTURE,
    UI_PAGE_ALERTS,
    UI_PAGE_TUNE,
    UI_PAGE_AMBIENT,
    UI_PAGE_ABOUT,
    UI_PAGE_ENGINEER_RAW,
    UI_PAGE_ENGINEER_POWER,
};

static const ui_screen_def_t *ui_screen_by_id(uint8_t id)
{
    for (size_t i = 0; i < sizeof(k_ui_screens) / sizeof(k_ui_screens[0]); ++i)
    {
        if (k_ui_screens[i].id == id)
            return &k_ui_screens[i];
    }
    return NULL;
}

static uint8_t ui_layout_count(void)
{
    return (uint8_t)(sizeof(k_ui_layout) / sizeof(k_ui_layout[0]));
}

static uint8_t ui_layout_get(uint8_t index)
{
    uint8_t count = ui_layout_count();
    if (!count)
        return UI_PAGE_DASHBOARD;
    if (index >= count)
        return k_ui_layout[0];
    return k_ui_layout[index];
}

uint8_t ui_registry_count(void)
{
    return (uint8_t)(sizeof(k_ui_screens) / sizeof(k_ui_screens[0]));
}

uint8_t ui_registry_layout_count(void)
{
    return ui_layout_count();
}

uint8_t ui_registry_layout_get(uint8_t index)
{
    return ui_layout_get(index);
}

uint8_t ui_registry_index(uint8_t page)
{
    uint8_t count = ui_layout_count();
    for (uint8_t i = 0; i < count; ++i)
    {
        if (ui_layout_get(i) == page)
            return i;
    }
    return 0u;
}

const char *ui_page_name(uint8_t page)
{
    const ui_screen_def_t *screen = ui_screen_by_id(page);
    return screen ? screen->name : "unknown";
}

static void render_page(ui_render_ctx_t *ctx, const ui_model_t *m, uint16_t dist_d10, uint16_t wh_d10)
{
    const ui_screen_def_t *screen = ui_screen_by_id(m->page);
    if (!screen)
        screen = ui_screen_by_id(UI_PAGE_DASHBOARD);
    if (!screen || !screen->render_full)
        return;
    screen->render_full(ctx, m, dist_d10, wh_d10);
}

static void dirty_from_page(ui_dirty_t *d, const ui_model_t *m, const ui_model_t *p)
{
    if (m->theme != p->theme || m->page != p->page)
    {
        ui_dirty_full(d);
        return;
    }
    const ui_screen_def_t *screen = ui_screen_by_id(m->page);
    if (!screen || !screen->dirty_fn)
    {
        ui_dirty_full(d);
        return;
    }
    screen->dirty_fn(d, m, p);
}

uint8_t ui_page_from_buttons(uint8_t short_press, uint8_t long_press, uint8_t current_page)
{
    uint8_t count = ui_registry_layout_count();
    if (!count)
        return UI_PAGE_DASHBOARD;
    uint8_t idx = ui_registry_index(current_page);
    uint8_t nav_short = (uint8_t)(short_press & (UI_PAGE_BUTTON_RAW | UI_PAGE_BUTTON_POWER));
    uint8_t nav_long = (uint8_t)(long_press & (UI_PAGE_BUTTON_RAW | UI_PAGE_BUTTON_POWER));
    if (nav_long & UI_PAGE_BUTTON_RAW)
        return UI_PAGE_DASHBOARD;
    if (nav_short == (UI_PAGE_BUTTON_RAW | UI_PAGE_BUTTON_POWER))
        return current_page;
    if (nav_short & UI_PAGE_BUTTON_RAW)
    {
        idx = (idx == 0u) ? (uint8_t)(count - 1u) : (uint8_t)(idx - 1u);
        return ui_registry_layout_get(idx);
    }
    if (nav_short & UI_PAGE_BUTTON_POWER)
    {
        idx = (uint8_t)((idx + 1u) % count);
        return ui_registry_layout_get(idx);
    }
    return current_page;
}

void ui_init(ui_state_t *ui)
{
    if (!ui)
        return;
    uint8_t *p = (uint8_t *)ui;
    for (size_t i = 0; i < sizeof(*ui); ++i)
        p[i] = 0;
}

bool ui_tick(ui_state_t *ui, const ui_model_t *model, uint32_t now_ms, ui_trace_t *trace)
{
    if (!ui || !model)
        return false;
    if ((uint32_t)(now_ms - ui->last_tick_ms) < UI_TICK_MS)
        return false;

    ui_graph_sample(ui, model);

    uint32_t start_ms = now_ms;
    ui_dirty_t dirty = {0};
    uint8_t force_full = 0u;

    if (!ui->prev_valid)
    {
        force_full = 1u;
        ui->prev_valid = 1u;
    }

    dirty_from_page(&dirty, model, &ui->prev);
    if (force_full)
    {
        dirty.full = 1u;
        dirty.count = 1u;
        dirty.rects[0] = (ui_rect_t){0, 0, DISP_W, DISP_H};
    }

    uint16_t dist_d10 = trip_distance_d10(model);
    uint16_t wh_d10 = trip_wh_per_unit_d10(model);
    const ui_palette_t *palette = ui_theme_palette(model->theme);
    const ui_screen_def_t *screen = ui_screen_by_id(model->page);
    if (!screen)
        screen = ui_screen_by_id(UI_PAGE_DASHBOARD);

    ui->hash = 0xFFFFFFFFu;
    ui_render_ctx_t hash_ctx = {ui, palette, 1u, 0u, 0u};
    render_page(&hash_ctx, model, dist_d10, wh_d10);
    ui->hash = ~ui->hash;

    ui->draw_ops = 0u;
    if (dirty.count || dirty.full)
    {
        ui_render_ctx_t draw_ctx = {ui, palette, 0u, 1u, 1u};
#ifdef UI_PIXEL_SIM
        ui_pixel_sink_begin(now_ms, dirty.full);
#endif
        if (dirty.full || !screen || !screen->render_partial)
        {
            render_page(&draw_ctx, model, dist_d10, wh_d10);
        }
        else
        {
            screen->render_partial(&draw_ctx, model, dist_d10, wh_d10, &dirty);
        }
#ifdef UI_PIXEL_SIM
        ui_pixel_sink_end();
#endif
    }

    if (trace)
    {
        trace->hash = ui->hash;
        trace->dirty_count = dirty.count;
        trace->draw_ops = ui->draw_ops;
        trace->render_ms = (uint16_t)((now_ms - start_ms) & 0xFFFFu);
        trace->full = dirty.full;
        trace->page = model->page;
        trace->trip_distance_d10 = dist_d10;
        trace->trip_wh_per_unit_d10 = wh_d10;
    }

    ui->prev = *model;
    ui->last_tick_ms = now_ms;
    return true;
}

static void append_kv_i32(char **ptr, size_t *rem, const char *k, int32_t v)
{
    append_str(ptr, rem, k);
    append_char(ptr, rem, '=');
    append_i32(ptr, rem, v);
}

static void append_kv_u32(char **ptr, size_t *rem, const char *k, uint32_t v)
{
    append_str(ptr, rem, k);
    append_char(ptr, rem, '=');
    append_u32(ptr, rem, v);
}

static void append_kv_hex(char **ptr, size_t *rem, const char *k, uint32_t v)
{
    append_str(ptr, rem, k);
    append_char(ptr, rem, '=');
    append_str(ptr, rem, "0x");
    append_hex_u32(ptr, rem, v);
}

static void append_sp(char **ptr, size_t *rem)
{
    append_char(ptr, rem, ' ');
}

size_t ui_format_engineer_trace(char *out, size_t len, const ui_model_t *m)
{
    if (!out || len == 0 || !m)
        return 0;
    char *ptr = out;
    size_t rem = len;
    append_str(&ptr, &rem, "[TRACE] eng page=");
    append_u32(&ptr, &rem, m->page);
    append_sp(&ptr, &rem);
    append_kv_i32(&ptr, &rem, "spd", m->speed_dmph);
    append_sp(&ptr, &rem);
    append_kv_u32(&ptr, &rem, "rpm", m->rpm);
    append_sp(&ptr, &rem);
    append_kv_u32(&ptr, &rem, "cad", m->cadence_rpm);
    append_sp(&ptr, &rem);
    append_kv_u32(&ptr, &rem, "tq", m->torque_raw);
    append_sp(&ptr, &rem);
    append_kv_u32(&ptr, &rem, "thr", m->throttle_pct);
    append_sp(&ptr, &rem);
    append_kv_u32(&ptr, &rem, "brk", m->brake);
    append_sp(&ptr, &rem);
    append_kv_hex(&ptr, &rem, "btn", m->buttons);
    append_sp(&ptr, &rem);
    append_kv_u32(&ptr, &rem, "soc", m->soc_pct);
    append_sp(&ptr, &rem);
    append_kv_u32(&ptr, &rem, "err", m->err);
    append_sp(&ptr, &rem);
    append_kv_i32(&ptr, &rem, "bv", m->batt_dV);
    append_sp(&ptr, &rem);
    append_kv_i32(&ptr, &rem, "bi", m->batt_dA);
    append_sp(&ptr, &rem);
    append_kv_i32(&ptr, &rem, "phase", m->phase_dA);
    append_sp(&ptr, &rem);
    append_kv_i32(&ptr, &rem, "sag", m->sag_margin_dV);
    append_sp(&ptr, &rem);
    append_kv_u32(&ptr, &rem, "therm", m->thermal_state);
    append_sp(&ptr, &rem);
    append_kv_i32(&ptr, &rem, "temp", m->ctrl_temp_dC);
    append_sp(&ptr, &rem);
    append_kv_u32(&ptr, &rem, "limw", m->limit_power_w);
    append_sp(&ptr, &rem);
    append_kv_u32(&ptr, &rem, "lrsn", m->limit_reason);
#ifdef UI_TRACE_REGEN
    append_sp(&ptr, &rem);
    append_kv_u32(&ptr, &rem, "r_sup", m->regen_supported);
    append_sp(&ptr, &rem);
    append_kv_u32(&ptr, &rem, "r_lvl", m->regen_level);
    append_sp(&ptr, &rem);
    append_kv_u32(&ptr, &rem, "r_brk", m->regen_brake_level);
    append_sp(&ptr, &rem);
    append_kv_u32(&ptr, &rem, "r_w", m->regen_cmd_power_w);
    append_sp(&ptr, &rem);
    append_kv_u32(&ptr, &rem, "r_i", m->regen_cmd_current_dA);
#endif
    append_char(&ptr, &rem, '\n');
    return (size_t)(ptr - out);
}

size_t ui_format_dashboard_trace(char *out, size_t len, const ui_model_t *model,
                                  const ui_trace_t *trace, uint32_t now_ms)
{
    if (!out || len == 0 || !model || !trace)
        return 0;
    char *ptr = out;
    size_t rem = len;
    append_str(&ptr, &rem, "[TRACE] ui ");
    append_kv_u32(&ptr, &rem, "ms", now_ms);
    append_sp(&ptr, &rem);
    append_kv_u32(&ptr, &rem, "hash", trace->hash);
    append_sp(&ptr, &rem);
    append_kv_u32(&ptr, &rem, "dt", trace->render_ms);
    append_sp(&ptr, &rem);
    append_kv_u32(&ptr, &rem, "spd", model->speed_dmph);
    append_sp(&ptr, &rem);
    append_kv_u32(&ptr, &rem, "soc", model->soc_pct);
    append_sp(&ptr, &rem);
    append_kv_u32(&ptr, &rem, "cad", model->cadence_rpm);
    append_sp(&ptr, &rem);
    append_kv_u32(&ptr, &rem, "pwr", model->power_w);
    append_sp(&ptr, &rem);
    append_kv_i32(&ptr, &rem, "bv", model->batt_dV);
    append_sp(&ptr, &rem);
    append_kv_i32(&ptr, &rem, "bi", model->batt_dA);
    append_sp(&ptr, &rem);
    append_kv_u32(&ptr, &rem, "lrsn", model->limit_reason);
    append_sp(&ptr, &rem);
    append_kv_u32(&ptr, &rem, "limw", model->limit_power_w);
    append_sp(&ptr, &rem);
    append_kv_u32(&ptr, &rem, "page", trace->page);
    append_sp(&ptr, &rem);
    append_kv_u32(&ptr, &rem, "dirty", trace->dirty_count);
    append_sp(&ptr, &rem);
    append_kv_u32(&ptr, &rem, "ops", trace->draw_ops);
    append_char(&ptr, &rem, '\n');
    return (size_t)(ptr - out);
}

size_t ui_registry_format_trace(char *out, size_t len)
{
    if (!out || len == 0)
        return 0;
    char *ptr = out;
    size_t rem = len;
    append_str(&ptr, &rem, "[TRACE] ui-reg count=");
    append_u32(&ptr, &rem, ui_registry_count());
    append_str(&ptr, &rem, " layout=");
    uint8_t count = ui_registry_layout_count();
    for (uint8_t i = 0; i < count; ++i)
    {
        if (i)
            append_char(&ptr, &rem, ',');
        append_u32(&ptr, &rem, ui_registry_layout_get(i));
    }
    append_str(&ptr, &rem, " names=");
    for (uint8_t i = 0; i < count; ++i)
    {
        if (i)
            append_char(&ptr, &rem, ',');
        append_str(&ptr, &rem, ui_page_name(ui_registry_layout_get(i)));
    }
    append_char(&ptr, &rem, '\n');
    return (size_t)(ptr - out);
}
