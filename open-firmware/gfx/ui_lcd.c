#include "ui_lcd.h"

#include "ui_display.h"
#include "ui_draw_common.h"
#include "ui_font_bitmap.h"

#define LCD_CMD_ADDR  0x60000000u
#define LCD_DATA_ADDR 0x60020000u

static inline void lcd_cmd(uint8_t v)
{
    *(volatile uint16_t *)LCD_CMD_ADDR = (uint16_t)v;
}

static inline void lcd_data8(uint8_t v)
{
    *(volatile uint16_t *)LCD_DATA_ADDR = (uint16_t)v;
}

static inline void lcd_data16(uint16_t v)
{
    *(volatile uint16_t *)LCD_DATA_ADDR = v;
}

static void lcd_set_window(uint16_t x, uint16_t y, uint16_t w, uint16_t h)
{
    if (w == 0u || h == 0u)
        return;
    uint16_t x1 = (uint16_t)(x + w - 1u);
    uint16_t y1 = (uint16_t)(y + h - 1u);

    lcd_cmd(0x2Au); /* CASET */
    lcd_data8((uint8_t)(x >> 8));
    lcd_data8((uint8_t)(x & 0xFFu));
    lcd_data8((uint8_t)(x1 >> 8));
    lcd_data8((uint8_t)(x1 & 0xFFu));

    lcd_cmd(0x2Bu); /* PASET */
    lcd_data8((uint8_t)(y >> 8));
    lcd_data8((uint8_t)(y & 0xFFu));
    lcd_data8((uint8_t)(y1 >> 8));
    lcd_data8((uint8_t)(y1 & 0xFFu));

    lcd_cmd(0x2Cu); /* RAMWR */
}

static uint16_t clip_dim(uint16_t start, uint16_t dim, uint16_t max)
{
    if (start >= max)
        return 0u;
    if ((uint32_t)start + (uint32_t)dim > (uint32_t)max)
        return (uint16_t)(max - start);
    return dim;
}

void ui_lcd_fill_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color)
{
    w = clip_dim(x, w, DISP_W);
    h = clip_dim(y, h, DISP_H);
    if (w == 0u || h == 0u)
        return;

    lcd_set_window(x, y, w, h);
    uint32_t n = (uint32_t)w * (uint32_t)h;
    for (uint32_t i = 0; i < n; ++i)
        lcd_data16(color);
}

static void fill_hline(uint16_t x, uint16_t y, uint16_t w, uint16_t color)
{
    if (w == 0u)
        return;
    lcd_set_window(x, y, w, 1u);
    for (uint16_t i = 0; i < w; ++i)
        lcd_data16(color);
}

static void fill_hline_dither(uint16_t x, uint16_t y, uint16_t w, uint16_t c0, uint16_t c1, uint8_t level)
{
    if (w == 0u)
        return;
    lcd_set_window(x, y, w, 1u);
    for (uint16_t i = 0; i < w; ++i)
    {
        uint16_t px = (uint16_t)(x + i);
        lcd_data16(ui_draw_dither_pick(px, y, c0, c1, level));
    }
}

static void fill_rect_dither(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t c0, uint16_t c1, uint8_t level)
{
    if (w == 0u || h == 0u)
        return;
    lcd_set_window(x, y, w, h);
    for (uint16_t yy = 0; yy < h; ++yy)
    {
        uint16_t py = (uint16_t)(y + yy);
        for (uint16_t xx = 0; xx < w; ++xx)
        {
            uint16_t px = (uint16_t)(x + xx);
            lcd_data16(ui_draw_dither_pick(px, py, c0, c1, level));
        }
    }
}

static void lcd_fill_hline_cb(void *ctx, uint16_t x, uint16_t y, uint16_t w, uint16_t color)
{
    (void)ctx;
    fill_hline(x, y, w, color);
}

static void lcd_fill_hline_dither_cb(void *ctx, uint16_t x, uint16_t y, uint16_t w,
                                     uint16_t c0, uint16_t c1, uint8_t level)
{
    (void)ctx;
    fill_hline_dither(x, y, w, c0, c1, level);
}

static void lcd_fill_rect_cb(void *ctx, uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color)
{
    (void)ctx;
    ui_lcd_fill_rect(x, y, w, h, color);
}

static void lcd_fill_rect_dither_cb(void *ctx, uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                                    uint16_t c0, uint16_t c1, uint8_t level)
{
    (void)ctx;
    fill_rect_dither(x, y, w, h, c0, c1, level);
}

static const ui_draw_rect_ops_t k_lcd_rect_ops = {
    .fill_hline = lcd_fill_hline_cb,
    .fill_hline_dither = lcd_fill_hline_dither_cb,
    .fill_rect = lcd_fill_rect_cb,
    .fill_rect_dither = lcd_fill_rect_dither_cb,
};

void ui_lcd_fill_round_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color, uint8_t radius)
{
    w = clip_dim(x, w, DISP_W);
    h = clip_dim(y, h, DISP_H);
    if (w == 0u || h == 0u)
        return;
    ui_draw_fill_round_rect(&k_lcd_rect_ops, NULL, x, y, w, h, color, radius);
}

void ui_lcd_fill_round_rect_dither(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                                   uint16_t color, uint16_t alt, uint8_t radius, uint8_t level)
{
    w = clip_dim(x, w, DISP_W);
    h = clip_dim(y, h, DISP_H);
    if (w == 0u || h == 0u)
        return;
    ui_draw_fill_round_rect_dither(&k_lcd_rect_ops, NULL, x, y, w, h, color, alt, radius, level);
}

static void lcd_begin_window_cb(void *ctx, uint16_t x, uint16_t y, uint16_t w, uint16_t h)
{
    (void)ctx;
    lcd_set_window(x, y, w, h);
}

static void lcd_write_pixel_cb(void *ctx, uint16_t x, uint16_t y, uint16_t color)
{
    (void)ctx;
    (void)x;
    (void)y;
    lcd_data16(color);
}

static const ui_draw_pixel_writer_t k_lcd_pixel_writer = {
    .begin_window = lcd_begin_window_cb,
    .write_pixel = lcd_write_pixel_cb,
};

void ui_lcd_draw_ring_arc_a4(uint16_t clip_x, uint16_t clip_y, uint16_t clip_w, uint16_t clip_h,
                             int16_t cx, int16_t cy, uint16_t outer_r, uint16_t thickness,
                             int16_t start_deg_cw, uint16_t sweep_deg_cw,
                             uint16_t fg, uint16_t bg)
{
    ui_draw_ring_arc_a4(&k_lcd_pixel_writer, NULL, clip_x, clip_y, clip_w, clip_h, cx, cy,
                        outer_r, thickness, start_deg_cw, sweep_deg_cw, fg, bg);
}

void ui_lcd_draw_ring_gauge_a4(uint16_t clip_x, uint16_t clip_y, uint16_t clip_w, uint16_t clip_h,
                               int16_t cx, int16_t cy, uint16_t outer_r, uint16_t thickness,
                               int16_t start_deg_cw, uint16_t sweep_deg_cw, uint16_t active_sweep_deg_cw,
                               uint16_t fg_active, uint16_t fg_inactive, uint16_t bg)
{
    ui_draw_ring_gauge_a4(&k_lcd_pixel_writer, NULL, clip_x, clip_y, clip_w, clip_h, cx, cy,
                          outer_r, thickness, start_deg_cw, sweep_deg_cw, active_sweep_deg_cw,
                          fg_active, fg_inactive, bg);
}

static void stroke_plot(int x, int y, uint16_t color, void *user)
{
    (void)user;
    if (x < 0 || y < 0 || x >= (int)DISP_W || y >= (int)DISP_H)
        return;
    lcd_set_window((uint16_t)x, (uint16_t)y, 1u, 1u);
    lcd_data16(color);
}

static void stroke_rect(int x, int y, int w, int h, uint16_t color, void *user)
{
    (void)user;
    if (x < 0 || y < 0 || w <= 0 || h <= 0)
        return;
    ui_lcd_fill_rect((uint16_t)x, (uint16_t)y, (uint16_t)w, (uint16_t)h, color);
}

void ui_lcd_draw_text_stroke(uint16_t x, uint16_t y, const char *text, uint16_t fg, uint16_t bg)
{
    ui_font_bitmap_draw_text(stroke_plot, stroke_rect, NULL, (int)x, (int)y, text, fg, bg);
}

void ui_lcd_draw_text_sized(uint16_t x, uint16_t y, const char *text, ui_font_size_t size, uint16_t fg, uint16_t bg)
{
    ui_font_draw_text(stroke_plot, stroke_rect, NULL, (int)x, (int)y, text, size, fg, bg);
}

void ui_lcd_draw_value_stroke(uint16_t x, uint16_t y, const char *label, int32_t value, uint16_t fg, uint16_t bg)
{
    char buf[32];
    ui_draw_format_value(buf, sizeof(buf), label, (long)value);
    ui_lcd_draw_text_stroke(x, y, buf, fg, bg);
}

void ui_lcd_draw_big_digit_7seg(uint16_t x, uint16_t y, uint8_t digit, uint8_t scale, uint16_t color)
{
    ui_draw_big_digit_7seg(&k_lcd_rect_ops, NULL, x, y, digit, scale, color);
}

void ui_lcd_draw_battery_icon(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint8_t soc, uint16_t color, uint16_t bg)
{
    ui_draw_battery_icon_ops(&k_lcd_rect_ops, NULL, x, y, w, h, soc, color, bg);
}

void ui_lcd_draw_warning_icon(uint16_t x, uint16_t y, uint16_t color)
{
    ui_draw_warning_icon_ops(&k_lcd_rect_ops, NULL, x, y, color);
}
