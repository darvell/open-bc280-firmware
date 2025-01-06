#include "ui_pixel_sink.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ui_display.h"
#include "ui_draw_common.h"
#include "ui_font_bitmap.h"

static uint16_t g_fb[DISP_W * DISP_H];
static uint8_t g_frame_pending;
static uint32_t g_frame_counter;
static uint8_t g_inited;

static const char *get_outdir(void)
{
    const char *env = getenv("UI_LCD_OUTDIR");
    if (env && env[0])
        return env;
    env = getenv("BC280_LCD_OUTDIR");
    if (env && env[0])
        return env;
    /* Default relative to `open-firmware/` (where `make -C open-firmware sim-host` runs). */
    return "tests/host/lcd_out";
}
static void ensure_outdir(void)
{
    const char *dir = get_outdir();
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "mkdir -p %s", dir);
    (void)system(cmd);
}

static void clear_fb(uint16_t color)
{
    for (size_t i = 0; i < (size_t)DISP_W * DISP_H; ++i)
        g_fb[i] = color;
}

static void set_px(int x, int y, uint16_t color)
{
    if (x < 0 || y < 0 || x >= (int)DISP_W || y >= (int)DISP_H)
        return;
    g_fb[(size_t)y * DISP_W + (size_t)x] = color;
}

static void fill_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color)
{
    for (uint16_t yy = 0; yy < h; ++yy)
        for (uint16_t xx = 0; xx < w; ++xx)
            set_px((int)x + (int)xx, (int)y + (int)yy, color);
}

static void fill_rect_dither(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t c0, uint16_t c1, uint8_t level)
{
    for (uint16_t yy = 0; yy < h; ++yy)
    {
        int py = (int)y + (int)yy;
        for (uint16_t xx = 0; xx < w; ++xx)
        {
            int px = (int)x + (int)xx;
            set_px(px, py, ui_draw_dither_pick((uint16_t)px, (uint16_t)py, c0, c1, level));
        }
    }
}

static void draw_hline(int x, int y, int w, uint16_t color)
{
    for (int i = 0; i < w; ++i)
        set_px(x + i, y, color);
}

static void draw_hline_dither(int x, int y, int w, uint16_t c0, uint16_t c1, uint8_t level)
{
    for (int i = 0; i < w; ++i)
        set_px(x + i, y, ui_draw_dither_pick((uint16_t)(x + i), (uint16_t)y, c0, c1, level));
}

static void pixel_fill_hline_cb(void *ctx, uint16_t x, uint16_t y, uint16_t w, uint16_t color)
{
    (void)ctx;
    draw_hline((int)x, (int)y, (int)w, color);
}

static void pixel_fill_hline_dither_cb(void *ctx, uint16_t x, uint16_t y, uint16_t w,
                                       uint16_t c0, uint16_t c1, uint8_t level)
{
    (void)ctx;
    draw_hline_dither((int)x, (int)y, (int)w, c0, c1, level);
}

static void pixel_fill_rect_cb(void *ctx, uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color)
{
    (void)ctx;
    fill_rect(x, y, w, h, color);
}

static void pixel_fill_rect_dither_cb(void *ctx, uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                                      uint16_t c0, uint16_t c1, uint8_t level)
{
    (void)ctx;
    fill_rect_dither(x, y, w, h, c0, c1, level);
}

static const ui_draw_rect_ops_t k_pixel_rect_ops = {
    .fill_hline = pixel_fill_hline_cb,
    .fill_hline_dither = pixel_fill_hline_dither_cb,
    .fill_rect = pixel_fill_rect_cb,
    .fill_rect_dither = pixel_fill_rect_dither_cb,
};

static void pixel_write_pixel_cb(void *ctx, uint16_t x, uint16_t y, uint16_t color)
{
    (void)ctx;
    set_px((int)x, (int)y, color);
}

static const ui_draw_pixel_writer_t k_pixel_writer = {
    .begin_window = NULL,
    .write_pixel = pixel_write_pixel_cb,
};

static void stroke_plot(int x, int y, uint16_t color, void *user)
{
    (void)user;
    set_px(x, y, color);
}

static void stroke_rect(int x, int y, int w, int h, uint16_t color, void *user)
{
    (void)user;
    if (x < 0 || y < 0 || w <= 0 || h <= 0)
        return;
    fill_rect((uint16_t)x, (uint16_t)y, (uint16_t)w, (uint16_t)h, color);
}

static void write_ppm(void)
{
    ensure_outdir();
    const char *dir = get_outdir();
    char path[512];
    char latest[512];
    snprintf(path, sizeof(path), "%s/host_lcd_%04u.ppm", dir, (unsigned)g_frame_counter);
    snprintf(latest, sizeof(latest), "%s/host_lcd_latest.ppm", dir);
    FILE *f = fopen(path, "wb");
    if (!f)
        return;
    fprintf(f, "P6\n%u %u\n255\n", DISP_W, DISP_H);
    for (size_t i = 0; i < (size_t)DISP_W * DISP_H; ++i)
    {
        uint16_t c = g_fb[i];
        uint8_t r = (uint8_t)(((c >> 11) & 0x1F) * 255 / 31);
        uint8_t g = (uint8_t)(((c >> 5) & 0x3F) * 255 / 63);
        uint8_t b = (uint8_t)((c & 0x1F) * 255 / 31);
        fputc(r, f);
        fputc(g, f);
        fputc(b, f);
    }
    fclose(f);
    FILE *src = fopen(path, "rb");
    FILE *dst = fopen(latest, "wb");
    if (src && dst)
    {
        char buf[4096];
        size_t n;
        while ((n = fread(buf, 1, sizeof(buf), src)) > 0)
            fwrite(buf, 1, n, dst);
    }
    if (src)
        fclose(src);
    if (dst)
        fclose(dst);
    g_frame_counter++;
}
__attribute__((used)) void ui_pixel_sink_begin(uint32_t now_ms, uint8_t full)
{
    (void)now_ms;
    if (!g_inited)
    {
        clear_fb(0x0000u);
        g_inited = 1;
    }
    if (full)
        clear_fb(0x0000u);
    g_frame_pending = 0;
}

void ui_pixel_sink_end(void)
{
    if (g_frame_pending)
        write_ppm();
}

void ui_pixel_sink_draw_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color)
{
    fill_rect(x, y, w, h, color);
    g_frame_pending = 1;
}

void ui_pixel_sink_draw_round_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color, uint8_t radius)
{
    ui_draw_fill_round_rect(&k_pixel_rect_ops, NULL, x, y, w, h, color, radius);
    g_frame_pending = 1;
}

void ui_pixel_sink_draw_round_rect_dither(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                                          uint16_t color, uint16_t alt, uint8_t radius, uint8_t level)
{
    ui_draw_fill_round_rect_dither(&k_pixel_rect_ops, NULL, x, y, w, h, color, alt, radius, level);
    g_frame_pending = 1;
}

void ui_pixel_sink_draw_text(uint16_t x, uint16_t y, const char *text, uint16_t fg, uint16_t bg)
{
    ui_font_bitmap_draw_text(stroke_plot, stroke_rect, NULL, (int)x, (int)y, text, fg, bg);
    g_frame_pending = 1;
}

void ui_pixel_sink_draw_text_sized(uint16_t x, uint16_t y, const char *text, ui_font_size_t size, uint16_t fg, uint16_t bg)
{
    ui_font_draw_text(stroke_plot, stroke_rect, NULL, (int)x, (int)y, text, size, fg, bg);
    g_frame_pending = 1;
}

void ui_pixel_sink_draw_value(uint16_t x, uint16_t y, const char *label, int32_t value, uint16_t fg, uint16_t bg)
{
    char buf[32];
    ui_draw_format_value(buf, sizeof(buf), label, (long)value);
    ui_pixel_sink_draw_text(x, y, buf, fg, bg);
}

void ui_pixel_sink_draw_big_digit(uint16_t x, uint16_t y, uint8_t digit, uint8_t scale, uint16_t color)
{
    ui_draw_big_digit_7seg(&k_pixel_rect_ops, NULL, x, y, digit, scale, color);
    g_frame_pending = 1;
}
__attribute__((used)) void ui_pixel_sink_draw_battery_icon(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint8_t soc, uint16_t color, uint16_t bg)
{
    ui_draw_battery_icon_ops(&k_pixel_rect_ops, NULL, x, y, w, h, soc, color, bg);
    g_frame_pending = 1;
}
__attribute__((used)) void ui_pixel_sink_draw_warning_icon(uint16_t x, uint16_t y, uint16_t color)
{
    ui_draw_warning_icon_ops(&k_pixel_rect_ops, NULL, x, y, color);
    g_frame_pending = 1;
}
__attribute__((used)) void ui_pixel_sink_draw_ring_arc_a4(uint16_t clip_x, uint16_t clip_y, uint16_t clip_w, uint16_t clip_h,
                                    int16_t cx, int16_t cy, uint16_t outer_r, uint16_t thickness,
                                    int16_t start_deg_cw, uint16_t sweep_deg_cw,
                                    uint16_t fg, uint16_t bg)
{
    ui_draw_ring_arc_a4(&k_pixel_writer, NULL, clip_x, clip_y, clip_w, clip_h,
                        cx, cy, outer_r, thickness, start_deg_cw, sweep_deg_cw, fg, bg);
    g_frame_pending = 1;
}
__attribute__((used)) void ui_pixel_sink_draw_ring_gauge_a4(uint16_t clip_x, uint16_t clip_y, uint16_t clip_w, uint16_t clip_h,
                                      int16_t cx, int16_t cy, uint16_t outer_r, uint16_t thickness,
                                      int16_t start_deg_cw, uint16_t sweep_deg_cw, uint16_t active_sweep_deg_cw,
                                      uint16_t fg_active, uint16_t fg_inactive, uint16_t bg)
{
    ui_draw_ring_gauge_a4(&k_pixel_writer, NULL, clip_x, clip_y, clip_w, clip_h,
                          cx, cy, outer_r, thickness, start_deg_cw, sweep_deg_cw, active_sweep_deg_cw,
                          fg_active, fg_inactive, bg);
    g_frame_pending = 1;
}
