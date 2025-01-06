#ifndef UI_FONT_BITMAP_H
#define UI_FONT_BITMAP_H

#include <stdint.h>

#define UI_FONT_FIRST 32
#define UI_FONT_LAST 126
#define UI_FONT_COUNT 95

/* Font size identifiers */
typedef enum {
    UI_FONT_LARGE = 0,  /* 28px - Large digits (speed display) */
    UI_FONT_HEADER = 1,  /* 18px - Section headers */
    UI_FONT_BODY = 2,  /* 12px - Stats, values, general text */
    UI_FONT_SMALL = 3,  /* 9px - Units, fine print */
    UI_FONT_COUNT_SIZES = 4
} ui_font_size_t;

/* Per-size font metrics */
#define UI_FONT_LARGE_ASCENT 26
#define UI_FONT_LARGE_DESCENT 7
#define UI_FONT_LARGE_LINE_HEIGHT 33
#define UI_FONT_HEADER_ASCENT 17
#define UI_FONT_HEADER_DESCENT 5
#define UI_FONT_HEADER_LINE_HEIGHT 22
#define UI_FONT_BODY_ASCENT 12
#define UI_FONT_BODY_DESCENT 3
#define UI_FONT_BODY_LINE_HEIGHT 15
#define UI_FONT_SMALL_ASCENT 9
#define UI_FONT_SMALL_DESCENT 3
#define UI_FONT_SMALL_LINE_HEIGHT 12

typedef struct {
    uint16_t offset;  /* Byte offset into bitmap data */
    uint8_t w;        /* Width in pixels */
    uint8_t h;        /* Height in pixels */
    int8_t xoff;      /* X offset from cursor */
    int8_t yoff;      /* Y offset from baseline */
    uint8_t xadv;     /* X advance to next glyph */
} ui_font_glyph_t;

typedef struct {
    const ui_font_glyph_t *glyphs;
    const uint8_t *bits;
    uint8_t ascent;
    uint8_t descent;
    uint8_t line_height;
} ui_font_data_t;

extern const ui_font_data_t g_ui_fonts[UI_FONT_COUNT_SIZES];

static inline const ui_font_glyph_t *ui_font_glyph(ui_font_size_t size, char c) {
    if (size >= UI_FONT_COUNT_SIZES)
        size = UI_FONT_BODY;
    const ui_font_data_t *fd = &g_ui_fonts[size];
    if ((uint8_t)c < UI_FONT_FIRST || (uint8_t)c > UI_FONT_LAST)
        return &fd->glyphs[0]; /* space */
    return &fd->glyphs[(uint8_t)c - UI_FONT_FIRST];
}

static inline const ui_font_data_t *ui_font_get(ui_font_size_t size) {
    if (size >= UI_FONT_COUNT_SIZES)
        size = UI_FONT_BODY;
    return &g_ui_fonts[size];
}

static inline uint16_t ui_font_text_width(ui_font_size_t size, const char *text) {
    uint16_t w = 0;
    while (text && *text) {
        const ui_font_glyph_t *g = ui_font_glyph(size, *text++);
        w += g->xadv;
    }
    return w;
}

/* Backward compatibility aliases */
#define ui_font_bitmap_glyph_t ui_font_glyph_t
#define ui_font_bitmap_glyph(c) ui_font_glyph(UI_FONT_BODY, c)
#define ui_font_bitmap_text_width(t) ui_font_text_width(UI_FONT_BODY, t)
#define UI_FONT_BITMAP_ASCENT UI_FONT_BODY_ASCENT
#define UI_FONT_BITMAP_DESCENT UI_FONT_BODY_DESCENT
#define UI_FONT_BITMAP_LINE_HEIGHT UI_FONT_BODY_LINE_HEIGHT

/* Callback types for rendering */
typedef void (*ui_font_plot_fn)(int x, int y, uint16_t color, void *user);
typedef void (*ui_font_rect_fn)(int x, int y, int w, int h, uint16_t color, void *user);

/* Draw text at (x, y) with foreground and background colors.
 * y is the baseline position. */
void ui_font_draw_text(ui_font_plot_fn plot,
                       ui_font_rect_fn rect,
                       void *user,
                       int x, int y,
                       const char *text,
                       ui_font_size_t size,
                       uint16_t fg,
                       uint16_t bg);

/* Backward compatibility wrapper (uses BODY size) */
void ui_font_bitmap_draw_text(ui_font_plot_fn plot,
                              ui_font_rect_fn rect,
                              void *user,
                              int x, int y,
                              const char *text,
                              uint16_t fg,
                              uint16_t bg);

#endif /* UI_FONT_BITMAP_H */
