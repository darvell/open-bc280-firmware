#ifndef UI_FONT_H
#define UI_FONT_H

#include <stdint.h>

/* Bitmap font glyph and cell dimensions (all chars same size) */
#define UI_FONT_GLYPH_W 5u  /* Actual glyph width in pixels */
#define UI_FONT_GLYPH_H 7u  /* Actual glyph height in pixels */
#define UI_FONT_ADV_X   6u  /* Horizontal advance (includes 1px spacing) */
#define UI_FONT_ADV_Y   8u  /* Vertical advance (includes 1px spacing) */

/* Compile-time assertions: glyph fits within cell */
#define UI_FONT_STATIC_ASSERT(cond, msg) \
    typedef char ui_font_assert_##msg[(cond) ? 1 : -1]

UI_FONT_STATIC_ASSERT(UI_FONT_GLYPH_W <= UI_FONT_ADV_X, glyph_width_exceeds_advance);
UI_FONT_STATIC_ASSERT(UI_FONT_GLYPH_H <= UI_FONT_ADV_Y, glyph_height_exceeds_advance);

void ui_font_draw_char(void (*set_px)(int x, int y, uint16_t color, void *user),
                       void *user,
                       char c,
                       int x,
                       int y,
                       uint16_t color);

void ui_font_draw_text(void (*set_px)(int x, int y, uint16_t color, void *user),
                       void *user,
                       int x,
                       int y,
                       const char *text,
                       uint16_t color);

static inline uint16_t ui_font_text_width(const char *text)
{
    uint16_t n = 0;
    while (text && *text++)
        n++;
    return (uint16_t)(n * UI_FONT_ADV_X);
}

static inline uint16_t ui_font_text_height(void)
{
    return UI_FONT_ADV_Y;
}

#endif
