#ifndef UI_DRAW_COMMON_H
#define UI_DRAW_COMMON_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
    void (*fill_hline)(void *ctx, uint16_t x, uint16_t y, uint16_t w, uint16_t color);
    void (*fill_hline_dither)(void *ctx, uint16_t x, uint16_t y, uint16_t w,
                              uint16_t c0, uint16_t c1, uint8_t level);
    void (*fill_rect)(void *ctx, uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color);
    void (*fill_rect_dither)(void *ctx, uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                             uint16_t c0, uint16_t c1, uint8_t level);
} ui_draw_rect_ops_t;

typedef struct {
    void (*begin_window)(void *ctx, uint16_t x, uint16_t y, uint16_t w, uint16_t h);
    void (*write_pixel)(void *ctx, uint16_t x, uint16_t y, uint16_t color);
} ui_draw_pixel_writer_t;

void ui_draw_format_value(char *out, size_t len, const char *label, long value);

uint16_t ui_draw_dither_pick(uint16_t x, uint16_t y, uint16_t c0, uint16_t c1, uint8_t level);

void ui_draw_fill_round_rect(const ui_draw_rect_ops_t *ops, void *ctx, uint16_t x, uint16_t y,
                             uint16_t w, uint16_t h, uint16_t color, uint8_t radius);
void ui_draw_fill_round_rect_dither(const ui_draw_rect_ops_t *ops, void *ctx, uint16_t x, uint16_t y,
                                    uint16_t w, uint16_t h, uint16_t color, uint16_t alt,
                                    uint8_t radius, uint8_t level);
void ui_draw_big_digit_7seg(const ui_draw_rect_ops_t *ops, void *ctx, uint16_t x, uint16_t y,
                            uint8_t digit, uint8_t scale, uint16_t color);
void ui_draw_battery_icon_ops(const ui_draw_rect_ops_t *ops, void *ctx, uint16_t x, uint16_t y,
                          uint16_t w, uint16_t h, uint8_t soc, uint16_t color, uint16_t bg);
void ui_draw_warning_icon_ops(const ui_draw_rect_ops_t *ops, void *ctx, uint16_t x, uint16_t y, uint16_t color);

void ui_draw_ring_arc_a4(const ui_draw_pixel_writer_t *ops, void *ctx,
                         uint16_t clip_x, uint16_t clip_y, uint16_t clip_w, uint16_t clip_h,
                         int16_t cx, int16_t cy, uint16_t outer_r, uint16_t thickness,
                         int16_t start_deg_cw, uint16_t sweep_deg_cw,
                         uint16_t fg, uint16_t bg);
void ui_draw_ring_gauge_a4(const ui_draw_pixel_writer_t *ops, void *ctx,
                           uint16_t clip_x, uint16_t clip_y, uint16_t clip_w, uint16_t clip_h,
                           int16_t cx, int16_t cy, uint16_t outer_r, uint16_t thickness,
                           int16_t start_deg_cw, uint16_t sweep_deg_cw, uint16_t active_sweep_deg_cw,
                           uint16_t fg_active, uint16_t fg_inactive, uint16_t bg);

#endif
