#ifndef OPEN_FIRMWARE_UI_FONT_STROKE_H
#define OPEN_FIRMWARE_UI_FONT_STROKE_H

#include <stdint.h>

/* Stroke font configuration and dimensions */
#define UI_FONT_STROKE_SCALE 2
#define UI_FONT_STROKE_TRACK 1
#define UI_FONT_STROKE_MAX_Y 6

/* Glyph dimensions in pixels */
#define UI_FONT_STROKE_HEIGHT_PX ((UI_FONT_STROKE_MAX_Y * UI_FONT_STROKE_SCALE) + 1)
#define UI_FONT_STROKE_MIN_GLYPH_W_PX (1 * UI_FONT_STROKE_SCALE)  /* dot, colon */
#define UI_FONT_STROKE_MAX_GLYPH_W_PX (4 * UI_FONT_STROKE_SCALE)  /* M, W, % */
#define UI_FONT_STROKE_STD_GLYPH_W_PX (3 * UI_FONT_STROKE_SCALE)  /* most chars */
#define UI_FONT_STROKE_ADVANCE_MIN ((1 + UI_FONT_STROKE_TRACK) * UI_FONT_STROKE_SCALE)
#define UI_FONT_STROKE_ADVANCE_MAX ((4 + UI_FONT_STROKE_TRACK) * UI_FONT_STROKE_SCALE)

/* Compile-time assertions: stroke font dimensions are sensible */
#define UI_STROKE_STATIC_ASSERT(cond, msg) \
    typedef char ui_stroke_assert_##msg[(cond) ? 1 : -1]

UI_STROKE_STATIC_ASSERT(UI_FONT_STROKE_SCALE >= 1, scale_must_be_positive);
UI_STROKE_STATIC_ASSERT(UI_FONT_STROKE_HEIGHT_PX <= 64, height_too_large);
UI_STROKE_STATIC_ASSERT(UI_FONT_STROKE_MAX_GLYPH_W_PX <= 32, max_width_too_large);

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

/* Text height is constant for stroke font (no multi-line support) */
static inline uint16_t ui_font_stroke_text_height_px(void)
{
    return UI_FONT_STROKE_HEIGHT_PX;
}

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
