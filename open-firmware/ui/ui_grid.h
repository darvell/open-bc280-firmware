#ifndef UI_GRID_H
#define UI_GRID_H

#include <stdint.h>
#include "ui_display.h"

/*
 * Grid-based layout system for 240x320 display.
 *
 * Grid unit: 10px
 * Columns: 24 (0-23)
 * Rows: 32 (0-31)
 *
 * Screen zones (recommended):
 * - Top bar:   rows 0-2   (30px)  - status icons, mode indicators
 * - Hero:      rows 3-12  (100px) - big speed number, main content
 * - Stats:     rows 13-24 (120px) - 2x2 or 4-column stats
 * - Footer:    rows 25-31 (70px)  - secondary info
 */

#define UI_GRID_UNIT 10u

/* Grid dimensions */
#define UI_GRID_COLS (DISP_W / UI_GRID_UNIT)  /* 24 */
#define UI_GRID_ROWS (DISP_H / UI_GRID_UNIT)  /* 32 */

/* Compile-time assertions: grid evenly divides screen */
#define UI_GRID_STATIC_ASSERT(cond, msg) \
    typedef char ui_grid_assert_##msg[(cond) ? 1 : -1]

UI_GRID_STATIC_ASSERT(DISP_W % UI_GRID_UNIT == 0, width_must_divide_evenly);
UI_GRID_STATIC_ASSERT(DISP_H % UI_GRID_UNIT == 0, height_must_divide_evenly);
UI_GRID_STATIC_ASSERT(UI_GRID_COLS == 24, expect_24_columns);
UI_GRID_STATIC_ASSERT(UI_GRID_ROWS == 32, expect_32_rows);

/* Convert grid coordinates to pixels */
#define UI_GRID_X(col) ((uint16_t)((col) * UI_GRID_UNIT))
#define UI_GRID_Y(row) ((uint16_t)((row) * UI_GRID_UNIT))

/* Convert grid spans to pixel dimensions */
#define UI_GRID_W(col_span) ((uint16_t)((col_span) * UI_GRID_UNIT))
#define UI_GRID_HEIGHT(row_span) ((uint16_t)((row_span) * UI_GRID_UNIT))

/* Screen zone row definitions */
#define UI_ZONE_TOP_ROW     0u
#define UI_ZONE_TOP_ROWS    3u   /* rows 0-2 */
#define UI_ZONE_HERO_ROW    3u
#define UI_ZONE_HERO_ROWS   10u  /* rows 3-12 */
#define UI_ZONE_STATS_ROW   13u
#define UI_ZONE_STATS_ROWS  12u  /* rows 13-24 */
#define UI_ZONE_FOOTER_ROW  25u
#define UI_ZONE_FOOTER_ROWS 7u   /* rows 25-31 */

/* Screen zone pixel values (derived) */
#define UI_ZONE_TOP_Y       UI_GRID_Y(UI_ZONE_TOP_ROW)
#define UI_ZONE_TOP_H       UI_GRID_HEIGHT(UI_ZONE_TOP_ROWS)
#define UI_ZONE_HERO_Y      UI_GRID_Y(UI_ZONE_HERO_ROW)
#define UI_ZONE_HERO_H      UI_GRID_HEIGHT(UI_ZONE_HERO_ROWS)
#define UI_ZONE_STATS_Y     UI_GRID_Y(UI_ZONE_STATS_ROW)
#define UI_ZONE_STATS_H     UI_GRID_HEIGHT(UI_ZONE_STATS_ROWS)
#define UI_ZONE_FOOTER_Y    UI_GRID_Y(UI_ZONE_FOOTER_ROW)
#define UI_ZONE_FOOTER_H    UI_GRID_HEIGHT(UI_ZONE_FOOTER_ROWS)

/* Standard margins (1 grid unit) */
#define UI_GRID_MARGIN      UI_GRID_UNIT
#define UI_GRID_GAP         UI_GRID_UNIT

/* Grid cell definition */
typedef struct {
    uint8_t col;       /* Grid column (0 to UI_GRID_COLS-1) */
    uint8_t row;       /* Grid row (0 to UI_GRID_ROWS-1) */
    uint8_t col_span;  /* Width in grid units */
    uint8_t row_span;  /* Height in grid units */
} ui_grid_cell_t;

/* Convert grid cell to pixel rect */
#define UI_GRID_CELL_X(cell)  UI_GRID_X((cell).col)
#define UI_GRID_CELL_Y(cell)  UI_GRID_Y((cell).row)
#define UI_GRID_CELL_W(cell)  UI_GRID_W((cell).col_span)
#define UI_GRID_CELL_H(cell)  UI_GRID_HEIGHT((cell).row_span)

/* Compile-time bounds check: cell fits on screen */
#define UI_GRID_CELL_VALID(cell) \
    (((cell).col + (cell).col_span) <= UI_GRID_COLS && \
     ((cell).row + (cell).row_span) <= UI_GRID_ROWS)

/* Static cell definition with bounds check */
#define UI_GRID_CELL(c, r, cs, rs) \
    ((ui_grid_cell_t){(c), (r), (cs), (rs)})

/* Bounds-checked cell definition (compile-time error if out of bounds) */
#define UI_GRID_CELL_CHECKED(c, r, cs, rs) \
    (UI_GRID_STATIC_ASSERT((c) + (cs) <= UI_GRID_COLS, cell_col_overflow), \
     UI_GRID_STATIC_ASSERT((r) + (rs) <= UI_GRID_ROWS, cell_row_overflow), \
     UI_GRID_CELL(c, r, cs, rs))

/* Inline helper: convert grid cell to ui_rect_t (for use with existing UI code) */
static inline void ui_grid_to_rect(const ui_grid_cell_t *cell, uint16_t *x, uint16_t *y,
                                   uint16_t *w, uint16_t *h)
{
    if (x) *x = UI_GRID_CELL_X(*cell);
    if (y) *y = UI_GRID_CELL_Y(*cell);
    if (w) *w = UI_GRID_CELL_W(*cell);
    if (h) *h = UI_GRID_CELL_H(*cell);
}

/* Runtime bounds check */
static inline int ui_grid_cell_valid(const ui_grid_cell_t *cell)
{
    if (!cell) return 0;
    return UI_GRID_CELL_VALID(*cell);
}

#endif /* UI_GRID_H */
