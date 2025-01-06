#ifndef UI_PIXEL_SINK_H
#define UI_PIXEL_SINK_H

#include <stdint.h>
#include "ui_font_bitmap.h"

void ui_pixel_sink_begin(uint32_t now_ms, uint8_t full);
void ui_pixel_sink_end(void);

void ui_pixel_sink_draw_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color);
void ui_pixel_sink_draw_round_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color, uint8_t radius);
void ui_pixel_sink_draw_round_rect_dither(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                                          uint16_t color, uint16_t alt, uint8_t radius, uint8_t level);
void ui_pixel_sink_draw_text(uint16_t x, uint16_t y, const char *text, uint16_t fg, uint16_t bg);
void ui_pixel_sink_draw_text_sized(uint16_t x, uint16_t y, const char *text, ui_font_size_t size, uint16_t fg, uint16_t bg);
void ui_pixel_sink_draw_value(uint16_t x, uint16_t y, const char *label, int32_t value, uint16_t fg, uint16_t bg);
void ui_pixel_sink_draw_big_digit(uint16_t x, uint16_t y, uint8_t digit, uint8_t scale, uint16_t color);
void ui_pixel_sink_draw_battery_icon(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint8_t soc, uint16_t color, uint16_t bg);
void ui_pixel_sink_draw_warning_icon(uint16_t x, uint16_t y, uint16_t color);
void ui_pixel_sink_draw_ring_arc_a4(uint16_t clip_x, uint16_t clip_y, uint16_t clip_w, uint16_t clip_h,
                                    int16_t cx, int16_t cy, uint16_t outer_r, uint16_t thickness,
                                    int16_t start_deg_cw, uint16_t sweep_deg_cw,
                                    uint16_t fg, uint16_t bg);
void ui_pixel_sink_draw_ring_gauge_a4(uint16_t clip_x, uint16_t clip_y, uint16_t clip_w, uint16_t clip_h,
                                      int16_t cx, int16_t cy, uint16_t outer_r, uint16_t thickness,
                                      int16_t start_deg_cw, uint16_t sweep_deg_cw, uint16_t active_sweep_deg_cw,
                                      uint16_t fg_active, uint16_t fg_inactive, uint16_t bg);

#endif
