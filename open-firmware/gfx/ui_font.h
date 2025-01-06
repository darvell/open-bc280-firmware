#ifndef UI_FONT_H
#define UI_FONT_H

#include <stdint.h>

#define UI_FONT_GLYPH_W 5u
#define UI_FONT_GLYPH_H 7u
#define UI_FONT_ADV_X   6u
#define UI_FONT_ADV_Y   8u

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

#endif
