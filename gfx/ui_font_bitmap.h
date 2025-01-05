#ifndef UI_FONT_BITMAP_H
#define UI_FONT_BITMAP_H

#include <stdint.h>

#define UI_FONT_BITMAP_FIRST 32
#define UI_FONT_BITMAP_LAST 126
#define UI_FONT_BITMAP_COUNT 95
#define UI_FONT_BITMAP_ASCENT 12
#define UI_FONT_BITMAP_DESCENT 3
#define UI_FONT_BITMAP_LINE_HEIGHT 15

typedef struct {
    uint16_t offset;  /* Byte offset into bitmap data */
    uint8_t w;        /* Width in pixels */
    uint8_t h;        /* Height in pixels */
    int8_t xoff;      /* X offset from cursor */
    int8_t yoff;      /* Y offset from baseline */
    uint8_t xadv;     /* X advance to next glyph */
} ui_font_bitmap_glyph_t;

extern const ui_font_bitmap_glyph_t g_ui_font_bitmap_glyphs[UI_FONT_BITMAP_COUNT];
extern const uint8_t g_ui_font_bitmap_bits[];

static inline const ui_font_bitmap_glyph_t *ui_font_bitmap_glyph(char c) {
    if ((uint8_t)c < UI_FONT_BITMAP_FIRST || (uint8_t)c > UI_FONT_BITMAP_LAST)
        return &g_ui_font_bitmap_glyphs[0]; /* space */
    return &g_ui_font_bitmap_glyphs[(uint8_t)c - UI_FONT_BITMAP_FIRST];
}

static inline uint16_t ui_font_bitmap_text_width(const char *text) {
    uint16_t w = 0;
    while (text && *text) {
        const ui_font_bitmap_glyph_t *g = ui_font_bitmap_glyph(*text++);
        w += g->xadv;
    }
    return w;
}

/* Callback types for rendering */
typedef void (*ui_font_bitmap_plot_fn)(int x, int y, uint16_t color, void *user);
typedef void (*ui_font_bitmap_rect_fn)(int x, int y, int w, int h, uint16_t color, void *user);

/* Draw text at (x, y) with foreground and background colors.
 * y is the baseline position. */
void ui_font_bitmap_draw_text(ui_font_bitmap_plot_fn plot,
                              ui_font_bitmap_rect_fn rect,
                              void *user,
                              int x, int y,
                              const char *text,
                              uint16_t fg,
                              uint16_t bg);

#endif /* UI_FONT_BITMAP_H */
