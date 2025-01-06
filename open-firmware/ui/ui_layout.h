#ifndef UI_LAYOUT_H
#define UI_LAYOUT_H

/**
 * @file ui_layout.h
 * @brief Grid-based layout specifications for all UI screens
 *
 * Information Density Audit - Visual Hierarchy and Grouping
 *
 * PRINCIPLES:
 * 1. Primary info is instant - Speed readable in <0.5s glance
 * 2. Secondary info grouped - Related stats clustered
 * 3. Tertiary info discoverable - Available but not distracting
 * 4. White space is intentional - Breathing room, not wasted space
 *
 * VISUAL HIERARCHY (biggest to smallest):
 * 1. Speed number - dominates screen, biggest font (scale 5)
 * 2. Power/assist indicator - secondary importance
 * 3. Stats grid - compact but readable
 * 4. Status icons - small, peripheral
 *
 * INFORMATION GROUPING:
 * - Motion: Speed, power, assist level
 * - Energy: Battery %, voltage, current, Wh/mi
 * - Trip: Distance, time, avg speed
 * - System: BLE, motor status, errors
 *
 * DENSITY GUIDELINES:
 * - Dashboard: 5-7 data points (speed + 4-6 stats)
 * - Stats screen: 8-12 data points (detailed view)
 * - Settings: 4-6 options per page (touch-friendly)
 *
 * GRID SYSTEM: 10px unit = 24 columns x 32 rows (240x320 display)
 */

#include "ui_grid.h"
#include <stdint.h>

/*===========================================================================
 * DASHBOARD LAYOUT
 *
 * Visual hierarchy:
 *   [TOP BAR] Assist | Gear | Mode | SOC%  - rows 0-2 (30px)
 *   [HERO]    ~~~~~ SPEED ~~~~~            - rows 3-17 (150px)
 *             Power gauge arc behind
 *             Range estimate below digits
 *   [STATS]   VOLT | CUR                   - rows 18-25 (80px)
 *             TRIP | WH/MI
 *   [MARGIN]  bottom padding               - rows 26-31 (60px)
 *
 * Data points: 7 (Speed, SOC, Volt, Current, Trip, Efficiency, Assist)
 *===========================================================================*/

/* Dashboard zones (grid coordinates) */
#define UI_DASH_TOP_ROW         0u
#define UI_DASH_TOP_ROWS        3u    /* rows 0-2: 30px */
#define UI_DASH_HERO_ROW        3u
#define UI_DASH_HERO_ROWS       15u   /* rows 3-17: 150px */
#define UI_DASH_STATS_ROW       18u
#define UI_DASH_STATS_ROWS      8u    /* rows 18-25: 80px */

/* Dashboard margin (grid units) */
#define UI_DASH_MARGIN_COLS     1u    /* 10px side margins */

/* Dashboard speed card (hero area) */
#define UI_DASH_SPEED_COL       UI_DASH_MARGIN_COLS
#define UI_DASH_SPEED_COLS      (UI_GRID_COLS - 2u * UI_DASH_MARGIN_COLS) /* 22 cols */

/* Dashboard stats tray (2x2 grid) */
#define UI_DASH_STATS_COL       UI_DASH_MARGIN_COLS
#define UI_DASH_STATS_COLS      (UI_GRID_COLS - 2u * UI_DASH_MARGIN_COLS)
#define UI_DASH_STAT_CELL_COLS  11u   /* Half width for 2-column layout */
#define UI_DASH_STAT_CELL_ROWS  4u    /* 40px per stat cell */

/* Compile-time validation */
UI_GRID_STATIC_ASSERT(UI_DASH_TOP_ROW + UI_DASH_TOP_ROWS == UI_DASH_HERO_ROW,
                      dash_top_hero_contiguous);
UI_GRID_STATIC_ASSERT(UI_DASH_HERO_ROW + UI_DASH_HERO_ROWS == UI_DASH_STATS_ROW,
                      dash_hero_stats_contiguous);
UI_GRID_STATIC_ASSERT(UI_DASH_STATS_ROW + UI_DASH_STATS_ROWS <= UI_GRID_ROWS,
                      dash_stats_fits);

/*===========================================================================
 * TRIP LAYOUT
 *
 * Visual hierarchy:
 *   [HEADER]  TRIP icon + title            - rows 0-2 (30px)
 *   [CARDS]   8 stat cards in 2x4 grid     - rows 3-27 (250px)
 *
 * Information grouping (2x4 grid):
 *   Row 0: DIST    | MOVE    (distance + moving time)
 *   Row 1: AVG     | MAX     (speed stats)
 *   Row 2: ENERGY  | WH/unit (energy stats)
 *   Row 3: ASSIST  | GEAR    (assist stats)
 *
 * Data points: 8 (good density for detailed view)
 *===========================================================================*/

#define UI_TRIP_HEADER_ROW      0u
#define UI_TRIP_HEADER_ROWS     3u
#define UI_TRIP_GRID_ROW        3u
#define UI_TRIP_GRID_ROWS       25u

/* Trip cards (2 columns, 4 rows)
 * Actual: PAD=16px, gap=8px, card=(240-32-8)/2=100px
 * In grid: margin=16px (1.6u), gap=8px (0.8u), card=100px (10u)
 * Note: Actual layout uses fractional grid positioning
 */
#define UI_TRIP_MARGIN_COLS     2u    /* 20px side margins (includes gap absorption) */
#define UI_TRIP_CARD_COLS       10u   /* 100px card width */
#define UI_TRIP_CARD_ROWS       5u    /* 50px card height */
#define UI_TRIP_GAP_COLS        0u    /* Gap absorbed into margins */
#define UI_TRIP_GAP_ROWS        1u    /* 10px gap between rows */

/*===========================================================================
 * SETTINGS LAYOUT
 *
 * Visual hierarchy:
 *   [HEADER]  SETTINGS icon + title        - rows 0-2 (30px)
 *   [LIST]    6 menu items in single col   - rows 3-27 (250px)
 *
 * Touch-friendly: 32px row height, 6 items visible
 *
 * Data points: 6 options (appropriate for settings)
 *===========================================================================*/

#define UI_SETTINGS_HEADER_ROW   0u
#define UI_SETTINGS_HEADER_ROWS  3u
#define UI_SETTINGS_LIST_ROW     3u
#define UI_SETTINGS_LIST_ROWS    22u

/* Settings list items */
#define UI_SETTINGS_MARGIN_COLS  2u
#define UI_SETTINGS_ITEM_COLS    (UI_GRID_COLS - 2u * UI_SETTINGS_MARGIN_COLS)
#define UI_SETTINGS_ITEM_ROWS    3u   /* 30px per item */
#define UI_SETTINGS_ITEM_GAP     0u   /* No gap, use dividers */
#define UI_SETTINGS_MAX_ITEMS    6u

/*===========================================================================
 * POWER LAYOUT (Consolidated Battery + Thermal)
 *
 * Visual hierarchy:
 *   [HEADER]  POWER icon + title           - rows 0-2 (30px)
 *   [GAUGES]  SOC ring | Temp ring | Stats - rows 3-12 (100px)
 *   [RANGE]   Range estimate + limits      - rows 14-22 (90px)
 *
 * Information grouping:
 *   - Left gauge: Battery SOC (primary energy)
 *   - Center gauge: Temperature (thermal status)
 *   - Right panel: VOLT, CUR (electrical)
 *   - Bottom: RANGE, SAG, LIMIT, STATE (system status)
 *
 * Data points: 7 (SOC, Temp, Volt, Current, Range, Sag, Limit)
 *===========================================================================*/

#define UI_POWER_HEADER_ROW     0u
#define UI_POWER_HEADER_ROWS    3u
#define UI_POWER_GAUGE_ROW      3u
#define UI_POWER_GAUGE_ROWS     10u   /* 100px for gauge area */
#define UI_POWER_RANGE_ROW      14u
#define UI_POWER_RANGE_ROWS     9u    /* 90px for range card */

/* Power gauge zones (3 columns) */
#define UI_POWER_GAUGE_SOC_COL   2u
#define UI_POWER_GAUGE_SOC_COLS  8u   /* 80px for SOC ring */
#define UI_POWER_GAUGE_TEMP_COL  10u
#define UI_POWER_GAUGE_TEMP_COLS 8u   /* 80px for temp ring */
#define UI_POWER_GAUGE_STAT_COL  18u
#define UI_POWER_GAUGE_STAT_COLS 5u   /* 50px for stats */

/*===========================================================================
 * BATTERY LAYOUT (Detailed battery screen)
 *
 * Visual hierarchy:
 *   [HEADER]  BATTERY icon + title         - rows 0-2 (30px)
 *   [HERO]    SOC ring (large) | Stats     - rows 3-15 (130px)
 *   [RANGE]   Range + confidence bar       - rows 17-23 (70px)
 *
 * Data points: 6 (SOC, Volt, Current, Range, Sag, Confidence)
 *===========================================================================*/

#define UI_BATT_HEADER_ROW      0u
#define UI_BATT_HEADER_ROWS     3u
#define UI_BATT_HERO_ROW        3u
#define UI_BATT_HERO_ROWS       13u
#define UI_BATT_RANGE_ROW       17u
#define UI_BATT_RANGE_ROWS      7u

/* Battery hero layout */
#define UI_BATT_GAUGE_COL       2u
#define UI_BATT_GAUGE_COLS      11u   /* 110px for large SOC ring */
#define UI_BATT_STAT_COL        14u
#define UI_BATT_STAT_COLS       9u    /* 90px for stats panel */

/*===========================================================================
 * COMMON HEADER LAYOUT
 *
 * All screens use a consistent header:
 *   [ICON]  [TITLE TEXT]
 *===========================================================================*/

#define UI_HEADER_ROW           0u
#define UI_HEADER_ROWS          3u    /* 30px consistent header */
#define UI_HEADER_ICON_COL      2u
#define UI_HEADER_ICON_COLS     2u    /* 20px icon */
#define UI_HEADER_TITLE_COL     5u
#define UI_HEADER_TITLE_COLS    17u   /* Remaining space for title */

/*===========================================================================
 * LAYOUT HELPER MACROS
 *===========================================================================*/

/* Create a ui_grid_cell_t for a screen zone */
#define UI_LAYOUT_ZONE(name) \
    UI_GRID_CELL(UI_##name##_COL, UI_##name##_ROW, \
                 UI_##name##_COLS, UI_##name##_ROWS)

/* Convert layout zone to pixel rect */
#define UI_LAYOUT_RECT(cell) \
    ((ui_rect_t){ \
        UI_GRID_CELL_X(cell), \
        UI_GRID_CELL_Y(cell), \
        UI_GRID_CELL_W(cell), \
        UI_GRID_CELL_H(cell)  \
    })

/*===========================================================================
 * AUDIT FINDINGS
 *
 * Dashboard:
 *   [OK] Speed is the hero (scale 5 digits = ~100px height)
 *   [OK] Stats tray shows 4 key metrics (volt, current, trip, efficiency)
 *   [OK] Top bar shows assist/gear/SOC (periphery, doesn't compete)
 *   [OK] Power gauge arc provides visual feedback without numbers
 *
 * Trip:
 *   [OK] 8 data points - appropriate for detailed stats view
 *   [OK] 2x4 grid provides clear structure
 *   [OK] Related stats are adjacent (speed stats, energy stats)
 *
 * Settings:
 *   [OK] 6 items - appropriate density
 *   [OK] Touch-friendly row height (32px)
 *   [OK] Clear selection highlight
 *
 * Power (consolidated):
 *   [OK] Two gauges give quick visual status
 *   [OK] 7 data points - good balance
 *   [OK] Groups: energy (left), thermal (center), electrical (right)
 *
 * Battery:
 *   [OK] Large SOC gauge dominates (primary info)
 *   [OK] Volt/Current secondary but accessible
 *   [OK] Range estimate with confidence bar is discoverable
 *
 * RECOMMENDATIONS:
 *   - All screens follow density guidelines
 *   - Visual hierarchy is consistent
 *   - Consider adding grid-based layout functions in future refactor
 *===========================================================================*/

#endif /* UI_LAYOUT_H */
