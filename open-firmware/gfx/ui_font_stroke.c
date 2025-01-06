#include "ui_font_stroke.h"

#include <stddef.h>

#define ARR_LEN(a) (sizeof(a) / sizeof((a)[0]))
#define SEG(x0_, y0_, x1_, y1_) { (int8_t)(x0_), (int8_t)(y0_), (int8_t)(x1_), (int8_t)(y1_) }

static const ui_font_stroke_seg_t k_glyph_A[] = {
    SEG(0, 6, 1, 0),
    SEG(2, 6, 1, 0),
    SEG(0, 3, 2, 3),
};
static const ui_font_stroke_seg_t k_glyph_B[] = {
    SEG(0, 0, 0, 6),
    SEG(0, 0, 2, 0),
    SEG(0, 3, 2, 3),
    SEG(0, 6, 2, 6),
    SEG(2, 0, 2, 3),
    SEG(2, 3, 2, 6),
};
static const ui_font_stroke_seg_t k_glyph_C[] = {
    SEG(0, 0, 0, 6),
    SEG(0, 0, 2, 0),
    SEG(0, 6, 2, 6),
};
static const ui_font_stroke_seg_t k_glyph_D[] = {
    SEG(0, 0, 0, 6),
    SEG(0, 0, 2, 0),
    SEG(0, 6, 2, 6),
    SEG(2, 1, 2, 5),
};
static const ui_font_stroke_seg_t k_glyph_E[] = {
    SEG(0, 0, 0, 6),
    SEG(0, 0, 2, 0),
    SEG(0, 3, 2, 3),
    SEG(0, 6, 2, 6),
};
static const ui_font_stroke_seg_t k_glyph_F[] = {
    SEG(0, 0, 0, 6),
    SEG(0, 0, 2, 0),
    SEG(0, 3, 2, 3),
};
static const ui_font_stroke_seg_t k_glyph_G[] = {
    SEG(0, 0, 0, 6),
    SEG(0, 0, 2, 0),
    SEG(0, 6, 2, 6),
    SEG(1, 3, 2, 3),
    SEG(2, 3, 2, 6),
};
static const ui_font_stroke_seg_t k_glyph_H[] = {
    SEG(0, 0, 0, 6),
    SEG(2, 0, 2, 6),
    SEG(0, 3, 2, 3),
};
static const ui_font_stroke_seg_t k_glyph_I[] = {
    SEG(0, 0, 2, 0),
    SEG(1, 0, 1, 6),
    SEG(0, 6, 2, 6),
};
static const ui_font_stroke_seg_t k_glyph_J[] = {
    SEG(0, 0, 2, 0),
    SEG(2, 0, 2, 5),
    SEG(0, 6, 2, 6),
    SEG(0, 4, 0, 6),
};
static const ui_font_stroke_seg_t k_glyph_K[] = {
    SEG(0, 0, 0, 6),
    SEG(2, 0, 0, 3),
    SEG(0, 3, 2, 6),
};
static const ui_font_stroke_seg_t k_glyph_L[] = {
    SEG(0, 0, 0, 6),
    SEG(0, 6, 2, 6),
};
static const ui_font_stroke_seg_t k_glyph_M[] = {
    SEG(0, 6, 0, 0),
    SEG(3, 6, 3, 0),
    SEG(0, 0, 1, 3),
    SEG(3, 0, 2, 3),
};
static const ui_font_stroke_seg_t k_glyph_N[] = {
    SEG(0, 6, 0, 0),
    SEG(2, 6, 2, 0),
    SEG(0, 6, 2, 0),
};
static const ui_font_stroke_seg_t k_glyph_O[] = {
    SEG(0, 0, 0, 6),
    SEG(2, 0, 2, 6),
    SEG(0, 0, 2, 0),
    SEG(0, 6, 2, 6),
};
static const ui_font_stroke_seg_t k_glyph_P[] = {
    SEG(0, 0, 0, 6),
    SEG(0, 0, 2, 0),
    SEG(0, 3, 2, 3),
    SEG(2, 0, 2, 3),
};
static const ui_font_stroke_seg_t k_glyph_Q[] = {
    SEG(0, 0, 0, 6),
    SEG(2, 0, 2, 6),
    SEG(0, 0, 2, 0),
    SEG(0, 6, 2, 6),
    SEG(1, 4, 2, 6),
};
static const ui_font_stroke_seg_t k_glyph_R[] = {
    SEG(0, 0, 0, 6),
    SEG(0, 0, 2, 0),
    SEG(0, 3, 2, 3),
    SEG(2, 0, 2, 3),
    SEG(0, 3, 2, 6),
};
static const ui_font_stroke_seg_t k_glyph_S[] = {
    SEG(0, 0, 2, 0),
    SEG(0, 0, 0, 3),
    SEG(0, 3, 2, 3),
    SEG(2, 3, 2, 6),
    SEG(0, 6, 2, 6),
};
static const ui_font_stroke_seg_t k_glyph_T[] = {
    SEG(0, 0, 2, 0),
    SEG(1, 0, 1, 6),
};
static const ui_font_stroke_seg_t k_glyph_U[] = {
    SEG(0, 0, 0, 5),
    SEG(2, 0, 2, 5),
    SEG(0, 6, 2, 6),
};
static const ui_font_stroke_seg_t k_glyph_V[] = {
    SEG(0, 0, 1, 6),
    SEG(2, 0, 1, 6),
};
static const ui_font_stroke_seg_t k_glyph_W[] = {
    SEG(0, 0, 1, 6),
    SEG(1, 6, 2, 0),
    SEG(2, 0, 3, 6),
};
static const ui_font_stroke_seg_t k_glyph_X[] = {
    SEG(0, 0, 2, 6),
    SEG(2, 0, 0, 6),
};
static const ui_font_stroke_seg_t k_glyph_Y[] = {
    SEG(0, 0, 1, 3),
    SEG(2, 0, 1, 3),
    SEG(1, 3, 1, 6),
};
static const ui_font_stroke_seg_t k_glyph_Z[] = {
    SEG(0, 0, 2, 0),
    SEG(2, 0, 0, 6),
    SEG(0, 6, 2, 6),
};

static const ui_font_stroke_seg_t k_glyph_0[] = {
    SEG(0, 0, 0, 6),
    SEG(2, 0, 2, 6),
    SEG(0, 0, 2, 0),
    SEG(0, 6, 2, 6),
};
static const ui_font_stroke_seg_t k_glyph_1[] = {
    SEG(1, 0, 1, 6),
    SEG(0, 6, 2, 6),
};
static const ui_font_stroke_seg_t k_glyph_2[] = {
    SEG(0, 0, 2, 0),
    SEG(2, 0, 2, 3),
    SEG(0, 3, 2, 3),
    SEG(0, 3, 0, 6),
    SEG(0, 6, 2, 6),
};
static const ui_font_stroke_seg_t k_glyph_3[] = {
    SEG(0, 0, 2, 0),
    SEG(0, 3, 2, 3),
    SEG(0, 6, 2, 6),
    SEG(2, 0, 2, 6),
};
static const ui_font_stroke_seg_t k_glyph_4[] = {
    SEG(0, 0, 0, 3),
    SEG(0, 3, 2, 3),
    SEG(2, 0, 2, 6),
};
static const ui_font_stroke_seg_t k_glyph_5[] = {
    SEG(0, 0, 2, 0),
    SEG(0, 0, 0, 3),
    SEG(0, 3, 2, 3),
    SEG(2, 3, 2, 6),
    SEG(0, 6, 2, 6),
};
static const ui_font_stroke_seg_t k_glyph_6[] = {
    SEG(0, 0, 2, 0),
    SEG(0, 0, 0, 6),
    SEG(0, 3, 2, 3),
    SEG(2, 3, 2, 6),
    SEG(0, 6, 2, 6),
};
static const ui_font_stroke_seg_t k_glyph_7[] = {
    SEG(0, 0, 2, 0),
    SEG(2, 0, 0, 6),
};
static const ui_font_stroke_seg_t k_glyph_8[] = {
    SEG(0, 0, 0, 6),
    SEG(2, 0, 2, 6),
    SEG(0, 0, 2, 0),
    SEG(0, 6, 2, 6),
    SEG(0, 3, 2, 3),
};
static const ui_font_stroke_seg_t k_glyph_9[] = {
    SEG(0, 0, 2, 0),
    SEG(0, 0, 0, 3),
    SEG(0, 3, 2, 3),
    SEG(2, 0, 2, 6),
    SEG(0, 6, 2, 6),
};

static const ui_font_stroke_seg_t k_glyph_dash[] = {
    SEG(0, 3, 2, 3),
};
static const ui_font_stroke_seg_t k_glyph_dot[] = {
    SEG(0, 6, 0, 6),
};
static const ui_font_stroke_seg_t k_glyph_slash[] = {
    SEG(0, 6, 2, 0),
};
static const ui_font_stroke_seg_t k_glyph_percent[] = {
    SEG(0, 0, 0, 0),
    SEG(3, 6, 3, 6),
    SEG(0, 6, 3, 0),
};
static const ui_font_stroke_seg_t k_glyph_colon[] = {
    SEG(0, 2, 0, 2),
    SEG(0, 4, 0, 4),
};
static const ui_font_stroke_seg_t k_glyph_semicolon[] = {
    SEG(0, 2, 0, 2),
    SEG(0, 4, 0, 6),
};
static const ui_font_stroke_seg_t k_glyph_question[] = {
    SEG(0, 0, 2, 0),
    SEG(2, 0, 1, 3),
    SEG(1, 6, 1, 6),
};

static const ui_font_stroke_glyph_t g_glyph_A = {3, ARR_LEN(k_glyph_A), k_glyph_A};
static const ui_font_stroke_glyph_t g_glyph_B = {3, ARR_LEN(k_glyph_B), k_glyph_B};
static const ui_font_stroke_glyph_t g_glyph_C = {3, ARR_LEN(k_glyph_C), k_glyph_C};
static const ui_font_stroke_glyph_t g_glyph_D = {3, ARR_LEN(k_glyph_D), k_glyph_D};
static const ui_font_stroke_glyph_t g_glyph_E = {3, ARR_LEN(k_glyph_E), k_glyph_E};
static const ui_font_stroke_glyph_t g_glyph_F = {3, ARR_LEN(k_glyph_F), k_glyph_F};
static const ui_font_stroke_glyph_t g_glyph_G = {3, ARR_LEN(k_glyph_G), k_glyph_G};
static const ui_font_stroke_glyph_t g_glyph_H = {3, ARR_LEN(k_glyph_H), k_glyph_H};
static const ui_font_stroke_glyph_t g_glyph_I = {3, ARR_LEN(k_glyph_I), k_glyph_I};
static const ui_font_stroke_glyph_t g_glyph_J = {3, ARR_LEN(k_glyph_J), k_glyph_J};
static const ui_font_stroke_glyph_t g_glyph_K = {3, ARR_LEN(k_glyph_K), k_glyph_K};
static const ui_font_stroke_glyph_t g_glyph_L = {3, ARR_LEN(k_glyph_L), k_glyph_L};
static const ui_font_stroke_glyph_t g_glyph_M = {4, ARR_LEN(k_glyph_M), k_glyph_M};
static const ui_font_stroke_glyph_t g_glyph_N = {3, ARR_LEN(k_glyph_N), k_glyph_N};
static const ui_font_stroke_glyph_t g_glyph_O = {3, ARR_LEN(k_glyph_O), k_glyph_O};
static const ui_font_stroke_glyph_t g_glyph_P = {3, ARR_LEN(k_glyph_P), k_glyph_P};
static const ui_font_stroke_glyph_t g_glyph_Q = {3, ARR_LEN(k_glyph_Q), k_glyph_Q};
static const ui_font_stroke_glyph_t g_glyph_R = {3, ARR_LEN(k_glyph_R), k_glyph_R};
static const ui_font_stroke_glyph_t g_glyph_S = {3, ARR_LEN(k_glyph_S), k_glyph_S};
static const ui_font_stroke_glyph_t g_glyph_T = {3, ARR_LEN(k_glyph_T), k_glyph_T};
static const ui_font_stroke_glyph_t g_glyph_U = {3, ARR_LEN(k_glyph_U), k_glyph_U};
static const ui_font_stroke_glyph_t g_glyph_V = {3, ARR_LEN(k_glyph_V), k_glyph_V};
static const ui_font_stroke_glyph_t g_glyph_W = {4, ARR_LEN(k_glyph_W), k_glyph_W};
static const ui_font_stroke_glyph_t g_glyph_X = {3, ARR_LEN(k_glyph_X), k_glyph_X};
static const ui_font_stroke_glyph_t g_glyph_Y = {3, ARR_LEN(k_glyph_Y), k_glyph_Y};
static const ui_font_stroke_glyph_t g_glyph_Z = {3, ARR_LEN(k_glyph_Z), k_glyph_Z};

static const ui_font_stroke_glyph_t g_glyph_0 = {3, ARR_LEN(k_glyph_0), k_glyph_0};
static const ui_font_stroke_glyph_t g_glyph_1 = {3, ARR_LEN(k_glyph_1), k_glyph_1};
static const ui_font_stroke_glyph_t g_glyph_2 = {3, ARR_LEN(k_glyph_2), k_glyph_2};
static const ui_font_stroke_glyph_t g_glyph_3 = {3, ARR_LEN(k_glyph_3), k_glyph_3};
static const ui_font_stroke_glyph_t g_glyph_4 = {3, ARR_LEN(k_glyph_4), k_glyph_4};
static const ui_font_stroke_glyph_t g_glyph_5 = {3, ARR_LEN(k_glyph_5), k_glyph_5};
static const ui_font_stroke_glyph_t g_glyph_6 = {3, ARR_LEN(k_glyph_6), k_glyph_6};
static const ui_font_stroke_glyph_t g_glyph_7 = {3, ARR_LEN(k_glyph_7), k_glyph_7};
static const ui_font_stroke_glyph_t g_glyph_8 = {3, ARR_LEN(k_glyph_8), k_glyph_8};
static const ui_font_stroke_glyph_t g_glyph_9 = {3, ARR_LEN(k_glyph_9), k_glyph_9};

static const ui_font_stroke_glyph_t g_glyph_dash = {3, ARR_LEN(k_glyph_dash), k_glyph_dash};
static const ui_font_stroke_glyph_t g_glyph_dot = {1, ARR_LEN(k_glyph_dot), k_glyph_dot};
static const ui_font_stroke_glyph_t g_glyph_slash = {3, ARR_LEN(k_glyph_slash), k_glyph_slash};
static const ui_font_stroke_glyph_t g_glyph_percent = {4, ARR_LEN(k_glyph_percent), k_glyph_percent};
static const ui_font_stroke_glyph_t g_glyph_colon = {1, ARR_LEN(k_glyph_colon), k_glyph_colon};
static const ui_font_stroke_glyph_t g_glyph_semicolon = {1, ARR_LEN(k_glyph_semicolon), k_glyph_semicolon};
static const ui_font_stroke_glyph_t g_glyph_space = {2, 0, NULL};
static const ui_font_stroke_glyph_t g_glyph_question = {3, ARR_LEN(k_glyph_question), k_glyph_question};

static char normalize_char(char c)
{
    if (c >= 'a' && c <= 'z')
        return (char)(c - ('a' - 'A'));
    if (c == '\t')
        return ' ';
    return c;
}

const ui_font_stroke_glyph_t *ui_font_stroke_glyph(char c)
{
    switch (normalize_char(c))
    {
        case 'A': return &g_glyph_A;
        case 'B': return &g_glyph_B;
        case 'C': return &g_glyph_C;
        case 'D': return &g_glyph_D;
        case 'E': return &g_glyph_E;
        case 'F': return &g_glyph_F;
        case 'G': return &g_glyph_G;
        case 'H': return &g_glyph_H;
        case 'I': return &g_glyph_I;
        case 'J': return &g_glyph_J;
        case 'K': return &g_glyph_K;
        case 'L': return &g_glyph_L;
        case 'M': return &g_glyph_M;
        case 'N': return &g_glyph_N;
        case 'O': return &g_glyph_O;
        case 'P': return &g_glyph_P;
        case 'Q': return &g_glyph_Q;
        case 'R': return &g_glyph_R;
        case 'S': return &g_glyph_S;
        case 'T': return &g_glyph_T;
        case 'U': return &g_glyph_U;
        case 'V': return &g_glyph_V;
        case 'W': return &g_glyph_W;
        case 'X': return &g_glyph_X;
        case 'Y': return &g_glyph_Y;
        case 'Z': return &g_glyph_Z;
        case '0': return &g_glyph_0;
        case '1': return &g_glyph_1;
        case '2': return &g_glyph_2;
        case '3': return &g_glyph_3;
        case '4': return &g_glyph_4;
        case '5': return &g_glyph_5;
        case '6': return &g_glyph_6;
        case '7': return &g_glyph_7;
        case '8': return &g_glyph_8;
        case '9': return &g_glyph_9;
        case '-': return &g_glyph_dash;
        case '.': return &g_glyph_dot;
        case '/': return &g_glyph_slash;
        case '%': return &g_glyph_percent;
        case ':': return &g_glyph_colon;
        case ';': return &g_glyph_semicolon;
        case ' ': return &g_glyph_space;
        case '?': return &g_glyph_question;
        default: return &g_glyph_question;
    }
}

uint16_t ui_font_stroke_text_width_px(const char *text)
{
    if (!text)
        return 0;
    const char *s = text;
    uint16_t w = 0;
    while (*s)
    {
        const ui_font_stroke_glyph_t *g = ui_font_stroke_glyph(*s++);
        uint16_t adv = (uint16_t)((uint16_t)(g->width + UI_FONT_STROKE_TRACK) * (uint16_t)UI_FONT_STROKE_SCALE);
        w = (uint16_t)(w + adv);
    }
    return w;
}

static void draw_line(ui_font_stroke_plot_fn plot, void *user, int x0, int y0, int x1, int y1, uint16_t color)
{
    int dx = (x0 < x1) ? (x1 - x0) : (x0 - x1);
    int sx = (x0 < x1) ? 1 : -1;
    int dy = (y0 < y1) ? (y0 - y1) : (y1 - y0);
    int sy = (y0 < y1) ? 1 : -1;
    int err = dx + dy;

    for (;;)
    {
        plot(x0, y0, color, user);
        if (x0 == x1 && y0 == y1)
            break;
        int e2 = err * 2;
        if (e2 >= dy)
        {
            err += dy;
            x0 += sx;
        }
        if (e2 <= dx)
        {
            err += dx;
            y0 += sy;
        }
    }
}

void ui_font_stroke_draw_text(ui_font_stroke_plot_fn plot,
                              ui_font_stroke_rect_fn rect,
                              void *user,
                              int x, int y,
                              const char *text,
                              uint16_t fg,
                              uint16_t bg)
{
    if (!plot)
        return;
    const char *s = text ? text : "?";
    int pen_x = x;
    int pen_y = y;
    while (*s)
    {
        const ui_font_stroke_glyph_t *g = ui_font_stroke_glyph(*s++);
        int w_px = (int)((int)g->width * UI_FONT_STROKE_SCALE);
        if (rect && w_px > 0)
            rect(pen_x, pen_y, w_px, (int)UI_FONT_STROKE_HEIGHT_PX, bg, user);
        for (uint8_t i = 0; i < g->seg_count; ++i)
        {
            const ui_font_stroke_seg_t seg = g->segs[i];
            int x0 = pen_x + (int)seg.x0 * UI_FONT_STROKE_SCALE;
            int y0 = pen_y + (int)seg.y0 * UI_FONT_STROKE_SCALE;
            int x1 = pen_x + (int)seg.x1 * UI_FONT_STROKE_SCALE;
            int y1 = pen_y + (int)seg.y1 * UI_FONT_STROKE_SCALE;
            draw_line(plot, user, x0, y0, x1, y1, fg);
        }
        pen_x += (int)((g->width + UI_FONT_STROKE_TRACK) * UI_FONT_STROKE_SCALE);
    }
}
