#ifndef OPEN_FIRMWARE_UI_FONT_STROKE_H
#define OPEN_FIRMWARE_UI_FONT_STROKE_H

#include <stdint.h>

#define UI_FONT_STROKE_SCALE 2
#define UI_FONT_STROKE_TRACK 1
#define UI_FONT_STROKE_MAX_Y 6
#define UI_FONT_STROKE_HEIGHT_PX ((UI_FONT_STROKE_MAX_Y * UI_FONT_STROKE_SCALE) + 1)

typedef struct {
    int8_t x0;
    int8_t y0;
    int8_t x1;
    int8_t y1;
} ui_font_stroke_seg_t;

typedef struct {
    uint8_t width; /* grid units (max x + 1) */
    uint8_t seg_count;
    const ui_font_stroke_seg_t *segs;
} ui_font_stroke_glyph_t;

const ui_font_stroke_glyph_t *ui_font_stroke_glyph(char c);
uint16_t ui_font_stroke_text_width_px(const char *text);

typedef void (*ui_font_stroke_plot_fn)(int x, int y, uint16_t color, void *user);
typedef void (*ui_font_stroke_rect_fn)(int x, int y, int w, int h, uint16_t color, void *user);

void ui_font_stroke_draw_text(ui_font_stroke_plot_fn plot,
                              ui_font_stroke_rect_fn rect,
                              void *user,
                              int x, int y,
                              const char *text,
                              uint16_t fg,
                              uint16_t bg);

#endif
