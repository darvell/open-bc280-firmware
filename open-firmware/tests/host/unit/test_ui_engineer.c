#include <stdio.h>
#include <string.h>

#include "ui.h"
#include "ui_draw_common.h"
#include "ui_font_stroke.h"
#include "ui_grid.h"
#include "ui_layout.h"

static int expect_equal_str(const char *got, const char *want)
{
    if (strcmp(got, want) != 0)
    {
        fprintf(stderr, "UI TRACE MISMATCH\nwant: %s\ngot : %s\n", want, got);
        return 0;
    }
    return 1;
}

static int expect_true(int cond, const char *msg)
{
    if (!cond)
    {
        fprintf(stderr, "UI TEST FAIL: %s\n", msg);
        return 0;
    }
    return 1;
}

typedef struct {
    uint16_t w;
    uint16_t h;
    uint16_t *buf;
} test_surface_t;

static void surface_clear(test_surface_t *s, uint16_t color)
{
    if (!s || !s->buf)
        return;
    for (size_t i = 0; i < (size_t)s->w * (size_t)s->h; ++i)
        s->buf[i] = color;
}

static void surface_fill_hline(void *ctx, uint16_t x, uint16_t y, uint16_t w, uint16_t color)
{
    test_surface_t *s = (test_surface_t *)ctx;
    if (!s || !s->buf || y >= s->h || w == 0u)
        return;
    uint16_t x_end = x + w;
    if (x_end > s->w)
        x_end = s->w;
    for (uint16_t xx = x; xx < x_end; ++xx)
        s->buf[(size_t)y * s->w + xx] = color;
}

static void surface_fill_hline_dither(void *ctx, uint16_t x, uint16_t y, uint16_t w,
                                      uint16_t c0, uint16_t c1, uint8_t level)
{
    test_surface_t *s = (test_surface_t *)ctx;
    if (!s || !s->buf || y >= s->h || w == 0u)
        return;
    uint16_t x_end = x + w;
    if (x_end > s->w)
        x_end = s->w;
    for (uint16_t xx = x; xx < x_end; ++xx)
    {
        uint16_t color = ui_draw_dither_pick(xx, y, c0, c1, level);
        s->buf[(size_t)y * s->w + xx] = color;
    }
}

static void surface_fill_rect(void *ctx, uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color)
{
    test_surface_t *s = (test_surface_t *)ctx;
    if (!s || !s->buf || w == 0u || h == 0u)
        return;
    uint16_t y_end = y + h;
    uint16_t x_end = x + w;
    if (x_end > s->w)
        x_end = s->w;
    if (y_end > s->h)
        y_end = s->h;
    for (uint16_t yy = y; yy < y_end; ++yy)
        for (uint16_t xx = x; xx < x_end; ++xx)
            s->buf[(size_t)yy * s->w + xx] = color;
}

static void surface_fill_rect_dither(void *ctx, uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                                     uint16_t c0, uint16_t c1, uint8_t level)
{
    test_surface_t *s = (test_surface_t *)ctx;
    if (!s || !s->buf || w == 0u || h == 0u)
        return;
    uint16_t y_end = y + h;
    uint16_t x_end = x + w;
    if (x_end > s->w)
        x_end = s->w;
    if (y_end > s->h)
        y_end = s->h;
    for (uint16_t yy = y; yy < y_end; ++yy)
        for (uint16_t xx = x; xx < x_end; ++xx)
            s->buf[(size_t)yy * s->w + xx] = ui_draw_dither_pick(xx, yy, c0, c1, level);
}

static const ui_draw_rect_ops_t k_test_ops = {
    .fill_hline = surface_fill_hline,
    .fill_hline_dither = surface_fill_hline_dither,
    .fill_rect = surface_fill_rect,
    .fill_rect_dither = surface_fill_rect_dither,
};

static size_t surface_count_nonzero(const test_surface_t *s)
{
    if (!s || !s->buf)
        return 0u;
    size_t count = 0;
    for (size_t i = 0; i < (size_t)s->w * (size_t)s->h; ++i)
        if (s->buf[i])
            count++;
    return count;
}

static int test_draw_format_value(void)
{
    char buf[32];
    ui_draw_format_value(buf, sizeof(buf), "SPD", 123);
    if (!expect_equal_str(buf, "SPD 123"))
        return 0;
    ui_draw_format_value(buf, sizeof(buf), NULL, -42);
    if (!expect_equal_str(buf, "-42"))
        return 0;
    return 1;
}

static int test_draw_dither_pick(void)
{
    uint16_t c0 = 0x1111u;
    uint16_t c1 = 0x2222u;
    if (!expect_true(ui_draw_dither_pick(0, 0, c0, c1, 0u) == c0, "dither level 0 picks base"))
        return 0;
    if (!expect_true(ui_draw_dither_pick(0, 0, c0, c1, 1u) == c1, "dither level 1 picks alt at (0,0)"))
        return 0;
    if (!expect_true(ui_draw_dither_pick(1, 0, c0, c1, 1u) == c0, "dither level 1 keeps base at (1,0)"))
        return 0;
    return 1;
}

static int test_round_rect_solid(void)
{
    uint16_t buf[10u * 8u];
    test_surface_t s = {10u, 8u, buf};
    surface_clear(&s, 0u);
    ui_draw_fill_round_rect(&k_test_ops, &s, 2u, 1u, 5u, 4u, 0x1234u, 0u);
    for (uint16_t y = 0u; y < s.h; ++y)
    {
        for (uint16_t x = 0u; x < s.w; ++x)
        {
            uint16_t v = s.buf[(size_t)y * s.w + x];
            int inside = (x >= 2u && x < 7u && y >= 1u && y < 5u);
            if (inside && v != 0x1234u)
                return 0;
            if (!inside && v != 0u)
                return 0;
        }
    }
    return 1;
}

static int test_round_rect_dither_alt(void)
{
    uint16_t buf[8u * 6u];
    test_surface_t s = {8u, 6u, buf};
    surface_clear(&s, 0u);
    ui_draw_fill_round_rect_dither(&k_test_ops, &s, 1u, 1u, 4u, 3u, 0x1111u, 0x2222u, 0u, 16u);
    if (!expect_true(s.buf[2u * s.w + 2u] == 0x2222u, "dither level 16 uses alt color"))
        return 0;
    if (!expect_true(s.buf[0] == 0u, "dither draw stays in bounds"))
        return 0;
    return 1;
}

static int test_big_digit_variation(void)
{
    uint16_t buf[64u * 32u];
    test_surface_t s = {64u, 32u, buf};
    surface_clear(&s, 0u);
    ui_draw_big_digit_7seg(&k_test_ops, &s, 2u, 2u, 1u, 1u, 0xFFFFu);
    size_t count1 = surface_count_nonzero(&s);
    surface_clear(&s, 0u);
    ui_draw_big_digit_7seg(&k_test_ops, &s, 2u, 2u, 8u, 1u, 0xFFFFu);
    size_t count8 = surface_count_nonzero(&s);
    if (!expect_true(count1 > 0u, "digit 1 draws pixels"))
        return 0;
    if (!expect_true(count8 > count1, "digit 8 draws more pixels than 1"))
        return 0;
    return 1;
}

static int test_battery_icon_soc(void)
{
    uint16_t buf[40u * 20u];
    test_surface_t s = {40u, 20u, buf};
    surface_clear(&s, 0u);
    ui_draw_battery_icon_ops(&k_test_ops, &s, 2u, 2u, 30u, 12u, 0u, 0xFFFFu, 0x0000u);
    size_t empty = surface_count_nonzero(&s);
    surface_clear(&s, 0u);
    ui_draw_battery_icon_ops(&k_test_ops, &s, 2u, 2u, 30u, 12u, 100u, 0xFFFFu, 0x0000u);
    size_t full = surface_count_nonzero(&s);
    if (!expect_true(empty > 0u, "battery outline draws pixels"))
        return 0;
    if (!expect_true(full > empty, "battery fill grows with soc"))
        return 0;
    return 1;
}

static int test_warning_icon_pixels(void)
{
    uint16_t buf[16u * 16u];
    test_surface_t s = {16u, 16u, buf};
    surface_clear(&s, 0u);
    ui_draw_warning_icon_ops(&k_test_ops, &s, 0u, 0u, 0xFFFFu);
    if (!expect_true(s.buf[2u * s.w + 2u] == 0xFFFFu, "warning icon base fill"))
        return 0;
    if (!expect_true(s.buf[4u * s.w + 5u] == 0u, "warning icon punch-out"))
        return 0;
    return 1;
}

static void surface_set(test_surface_t *s, uint16_t x, uint16_t y, uint16_t color)
{
    if (!s || !s->buf)
        return;
    if (x >= s->w || y >= s->h)
        return;
    s->buf[(size_t)y * s->w + x] = color;
}

static void surface_write_pixel(void *ctx, uint16_t x, uint16_t y, uint16_t color)
{
    surface_set((test_surface_t *)ctx, x, y, color);
}

static const ui_draw_pixel_writer_t k_test_writer = {
    .begin_window = NULL,
    .write_pixel = surface_write_pixel,
};

static int test_ring_arc_full(void)
{
    uint16_t buf[20u * 20u];
    test_surface_t s = {20u, 20u, buf};
    surface_clear(&s, 0u);
    ui_draw_ring_arc_a4(&k_test_writer, &s, 0u, 0u, 20u, 20u, 10, 10, 4u, 4u, 0, 360u, 0xFFFFu, 0x0000u);
    if (!expect_true(s.buf[10u * s.w + 10u] == 0xFFFFu, "ring arc fills center on full sweep"))
        return 0;
    if (!expect_true(s.buf[0] == 0u, "ring arc clips outside"))
        return 0;
    return 1;
}

static int test_font_width_widest_chars(void)
{
    /* Test font width calculation for widest characters per bead open-bc280-firmware-mbp:
     * '0000' '88:88' 'WWWW' (widest chars) */

    /* Most chars have width 3, but M/W have width 4
     * Advance per char = (width + tracking) * scale = (w+1)*2 */
    uint16_t w_0000 = ui_font_stroke_text_width_px("0000");
    uint16_t w_8888 = ui_font_stroke_text_width_px("88:88");
    uint16_t w_wwww = ui_font_stroke_text_width_px("WWWW");

    /* '0' has width 3, advance = (3+1)*2 = 8 per char */
    uint16_t expected_0000 = 4u * 8u;  /* 32 pixels */
    if (!expect_true(w_0000 == expected_0000, "width of '0000' is 32px"))
    {
        fprintf(stderr, "  got: %u, want: %u\n", w_0000, expected_0000);
        return 0;
    }

    /* '8' has width 3 (adv 8), ':' has width 1 (adv 4) */
    /* 4 digits * 8 + 1 colon * 4 = 36 pixels */
    uint16_t expected_8888 = 4u * 8u + 1u * 4u;  /* 36 pixels */
    if (!expect_true(w_8888 == expected_8888, "width of '88:88' is 36px"))
    {
        fprintf(stderr, "  got: %u, want: %u\n", w_8888, expected_8888);
        return 0;
    }

    /* 'W' has width 4, advance = (4+1)*2 = 10 per char */
    uint16_t expected_wwww = 4u * 10u;  /* 40 pixels */
    if (!expect_true(w_wwww == expected_wwww, "width of 'WWWW' is 40px"))
    {
        fprintf(stderr, "  got: %u, want: %u\n", w_wwww, expected_wwww);
        return 0;
    }

    /* Verify height constant is accessible */
    if (!expect_true(ui_font_stroke_text_height_px() == UI_FONT_STROKE_HEIGHT_PX, "text height matches constant"))
        return 0;

    /* Verify big digit dimensions match expected scale */
    if (!expect_true(UI_BIG_DIGIT_WIDTH(1) == 12u, "big digit width scale 1"))
        return 0;
    if (!expect_true(UI_BIG_DIGIT_HEIGHT(1) == 20u, "big digit height scale 1"))
        return 0;
    if (!expect_true(UI_BIG_DIGIT_WIDTH(2) == 24u, "big digit width scale 2"))
        return 0;
    if (!expect_true(UI_BIG_DIGIT_HEIGHT(2) == 40u, "big digit height scale 2"))
        return 0;

    return 1;
}

static int test_layout_density(void)
{
    /* Test layout constants per bead open-bc280-firmware-ihc */

    /* Dashboard layout - zones must be contiguous and fit screen */
    if (!expect_true(UI_DASH_TOP_ROW == 0u, "dash top starts at row 0"))
        return 0;
    if (!expect_true(UI_DASH_TOP_ROW + UI_DASH_TOP_ROWS == UI_DASH_HERO_ROW,
                     "dash top/hero contiguous"))
        return 0;
    if (!expect_true(UI_DASH_HERO_ROW + UI_DASH_HERO_ROWS == UI_DASH_STATS_ROW,
                     "dash hero/stats contiguous"))
        return 0;
    if (!expect_true(UI_DASH_STATS_ROW + UI_DASH_STATS_ROWS <= UI_GRID_ROWS,
                     "dash stats fits screen"))
        return 0;

    /* Dashboard stats tray is 2 columns */
    if (!expect_true(UI_DASH_STAT_CELL_COLS * 2u <= UI_DASH_STATS_COLS,
                     "dash stats 2 columns fit"))
        return 0;

    /* Trip layout - 8 cards in 2x4 grid */
    if (!expect_true(UI_TRIP_CARD_COLS * 2u + UI_TRIP_GAP_COLS + UI_TRIP_MARGIN_COLS * 2u <= UI_GRID_COLS,
                     "trip 2 columns fit"))
        return 0;
    if (!expect_true(UI_TRIP_CARD_ROWS * 4u + UI_TRIP_GAP_ROWS * 3u + UI_TRIP_HEADER_ROWS <= UI_GRID_ROWS,
                     "trip 4 rows fit"))
        return 0;

    /* Settings layout - 6 items */
    if (!expect_true(UI_SETTINGS_ITEM_ROWS * UI_SETTINGS_MAX_ITEMS + UI_SETTINGS_HEADER_ROWS <= UI_GRID_ROWS,
                     "settings items fit"))
        return 0;

    /* Power layout - gauges and range fit */
    if (!expect_true(UI_POWER_GAUGE_ROW + UI_POWER_GAUGE_ROWS < UI_POWER_RANGE_ROW,
                     "power gauge above range"))
        return 0;
    if (!expect_true(UI_POWER_RANGE_ROW + UI_POWER_RANGE_ROWS <= UI_GRID_ROWS,
                     "power range fits screen"))
        return 0;

    /* Battery layout fits */
    if (!expect_true(UI_BATT_HERO_ROW + UI_BATT_HERO_ROWS < UI_BATT_RANGE_ROW,
                     "batt hero above range"))
        return 0;
    if (!expect_true(UI_BATT_RANGE_ROW + UI_BATT_RANGE_ROWS <= UI_GRID_ROWS,
                     "batt range fits screen"))
        return 0;

    /* Common header - consistent across screens */
    if (!expect_true(UI_HEADER_ROWS == 3u, "header is 3 grid rows (30px)"))
        return 0;

    return 1;
}

static int test_grid_layout_system(void)
{
    /* Test grid constants per bead open-bc280-firmware-ejw */

    /* Grid unit is 10px, screen is 240x320 */
    if (!expect_true(UI_GRID_UNIT == 10u, "grid unit is 10px"))
        return 0;
    if (!expect_true(UI_GRID_COLS == 24u, "24 columns"))
        return 0;
    if (!expect_true(UI_GRID_ROWS == 32u, "32 rows"))
        return 0;

    /* Test coordinate conversion */
    if (!expect_true(UI_GRID_X(0) == 0u, "col 0 -> x=0"))
        return 0;
    if (!expect_true(UI_GRID_X(12) == 120u, "col 12 -> x=120"))
        return 0;
    if (!expect_true(UI_GRID_X(24) == 240u, "col 24 -> x=240"))
        return 0;
    if (!expect_true(UI_GRID_Y(0) == 0u, "row 0 -> y=0"))
        return 0;
    if (!expect_true(UI_GRID_Y(16) == 160u, "row 16 -> y=160"))
        return 0;
    if (!expect_true(UI_GRID_Y(32) == 320u, "row 32 -> y=320"))
        return 0;

    /* Test span conversion */
    if (!expect_true(UI_GRID_W(1) == 10u, "1 col span = 10px"))
        return 0;
    if (!expect_true(UI_GRID_HEIGHT(3) == 30u, "3 row span = 30px"))
        return 0;

    /* Test screen zones */
    if (!expect_true(UI_ZONE_TOP_Y == 0u, "top zone y=0"))
        return 0;
    if (!expect_true(UI_ZONE_TOP_H == 30u, "top zone h=30"))
        return 0;
    if (!expect_true(UI_ZONE_HERO_Y == 30u, "hero zone y=30"))
        return 0;
    if (!expect_true(UI_ZONE_HERO_H == 100u, "hero zone h=100"))
        return 0;
    if (!expect_true(UI_ZONE_STATS_Y == 130u, "stats zone y=130"))
        return 0;
    if (!expect_true(UI_ZONE_STATS_H == 120u, "stats zone h=120"))
        return 0;
    if (!expect_true(UI_ZONE_FOOTER_Y == 250u, "footer zone y=250"))
        return 0;
    if (!expect_true(UI_ZONE_FOOTER_H == 70u, "footer zone h=70"))
        return 0;

    /* Test grid cell */
    ui_grid_cell_t cell = UI_GRID_CELL(2, 3, 10, 5);
    if (!expect_true(UI_GRID_CELL_X(cell) == 20u, "cell x=20"))
        return 0;
    if (!expect_true(UI_GRID_CELL_Y(cell) == 30u, "cell y=30"))
        return 0;
    if (!expect_true(UI_GRID_CELL_W(cell) == 100u, "cell w=100"))
        return 0;
    if (!expect_true(UI_GRID_CELL_H(cell) == 50u, "cell h=50"))
        return 0;

    /* Test bounds validation */
    ui_grid_cell_t valid_cell = UI_GRID_CELL(0, 0, 24, 32);
    if (!expect_true(ui_grid_cell_valid(&valid_cell), "full screen cell is valid"))
        return 0;

    ui_grid_cell_t overflow_cell = UI_GRID_CELL(20, 30, 10, 5);
    if (!expect_true(!ui_grid_cell_valid(&overflow_cell), "overflow cell is invalid"))
        return 0;

    /* Test helper function */
    uint16_t x, y, w, h;
    ui_grid_to_rect(&cell, &x, &y, &w, &h);
    if (!expect_true(x == 20u && y == 30u && w == 100u && h == 50u, "grid_to_rect conversion"))
        return 0;

    return 1;
}

static int page_in_layout(uint8_t page)
{
    uint8_t count = ui_registry_layout_count();
    for (uint8_t i = 0; i < count; ++i)
    {
        if (ui_registry_layout_get(i) == page)
            return 1;
    }
    return 0;
}

static size_t select_stable_pages(uint8_t *out, size_t cap)
{
    static const uint8_t candidates[] = {
        UI_PAGE_DASHBOARD,
        UI_PAGE_FOCUS,
        UI_PAGE_SETTINGS,
        UI_PAGE_PROFILES,
        UI_PAGE_ABOUT,
    };
    size_t n = 0;
    for (size_t i = 0; i < sizeof(candidates) / sizeof(candidates[0]); ++i)
    {
        if (n >= cap)
            break;
        if (page_in_layout(candidates[i]))
            out[n++] = candidates[i];
    }
    return n;
}

static void seed_model(ui_model_t *m)
{
    if (!m)
        return;
    m->speed_dmph = 123;
    m->rpm = 330;
    m->cadence_rpm = 88;
    m->torque_raw = 55;
    m->assist_mode = 2;
    m->virtual_gear = 3;
    m->soc_pct = 77;
    m->err = 0;
    m->batt_dV = 374;
    m->batt_dA = -12;
    m->power_w = 420;
    m->trip_distance_mm = 12000;
    m->trip_energy_mwh = 3400;
    m->trip_max_speed_dmph = 230;
    m->trip_avg_speed_dmph = 180;
    m->units = 0;
    m->theme = UI_THEME_DAY;
    m->mode = 0;
    m->settings_index = 0;
    m->profile_id = 1;
}

static int test_engineer_trace(void)
{
    char buf[256];
    ui_model_t m = {0};
    m.page = UI_PAGE_ENGINEER_RAW;
    m.speed_dmph = 123;
    m.rpm = 330;
    m.cadence_rpm = 88;
    m.torque_raw = 55;
    m.throttle_pct = 42;
    m.brake = 1;
    m.buttons = 0x0Cu;
    m.soc_pct = 77;
    m.err = 2;
    m.batt_dV = 374;
    m.batt_dA = -12;
    m.phase_dA = 234;
    m.sag_margin_dV = -18;
    m.thermal_state = 512;
    m.ctrl_temp_dC = 615;
    m.limit_power_w = 420;
    m.limit_reason = 3;

    size_t n = ui_format_engineer_trace(buf, sizeof(buf), &m);
    buf[(n < sizeof(buf)) ? n : (sizeof(buf) - 1)] = '\0';
    const char *want = "[TRACE] eng page=1 spd=123 rpm=330 cad=88 tq=55 thr=42 brk=1 btn=0x0000000c soc=77 err=2 bv=374 bi=-12 phase=234 sag=-18 therm=512 temp=615 limw=420 lrsn=3 r_sup=0 r_lvl=0 r_brk=0 r_w=0 r_i=0\n";
    if (!expect_equal_str(buf, want))
        return 0;

    m.page = UI_PAGE_ENGINEER_POWER;
    m.buttons = 0x08u;
    n = ui_format_engineer_trace(buf, sizeof(buf), &m);
    buf[(n < sizeof(buf)) ? n : (sizeof(buf) - 1)] = '\0';
    const char *want2 = "[TRACE] eng page=2 spd=123 rpm=330 cad=88 tq=55 thr=42 brk=1 btn=0x00000008 soc=77 err=2 bv=374 bi=-12 phase=234 sag=-18 therm=512 temp=615 limw=420 lrsn=3 r_sup=0 r_lvl=0 r_brk=0 r_w=0 r_i=0\n";
    if (!expect_equal_str(buf, want2))
        return 0;

    return 1;
}

static int test_ui_hash_determinism(void)
{
    uint8_t pages[3];
    size_t got = select_stable_pages(pages, 3);
    if (!expect_true(got >= 3, "stable UI pages < 3 in layout"))
        return 0;

    for (size_t i = 0; i < 3; ++i)
    {
        ui_state_t ui;
        ui_init(&ui);
        ui_model_t m = {0};
        seed_model(&m);
        m.page = pages[i];

        uint32_t now = 0;
        ui_trace_t t0;
        ui_trace_t t1;

        now += UI_TICK_MS;
        if (!ui_tick(&ui, &m, now, &t0))
            return 0;
        now += UI_TICK_MS;
        if (!ui_tick(&ui, &m, now, &t1))
            return 0;

        if (t0.hash == 0u || t1.hash == 0u)
        {
            fprintf(stderr, "UI HASH ZERO page=%u (%s)\n", pages[i], ui_page_name(pages[i]));
            return 0;
        }
        if (t0.hash != t1.hash)
        {
            fprintf(stderr, "UI HASH UNSTABLE page=%u (%s)\n", pages[i], ui_page_name(pages[i]));
            return 0;
        }
        if (t1.render_ms > UI_TICK_MS)
        {
            fprintf(stderr, "UI RENDER BUDGET EXCEEDED page=%u (%s) dt=%u\n",
                    pages[i], ui_page_name(pages[i]), t1.render_ms);
            return 0;
        }
    }
    return 1;
}

static int test_dashboard_dirty_budget(void)
{
    ui_state_t ui;
    ui_init(&ui);
    ui_model_t m = {0};
    seed_model(&m);
    m.page = UI_PAGE_DASHBOARD;

    uint32_t now = 0;
    ui_trace_t t0;
    ui_trace_t t1;

    now += UI_TICK_MS;
    if (!ui_tick(&ui, &m, now, &t0))
        return 0;

    m.speed_dmph += 10;
    now += UI_TICK_MS;
    if (!ui_tick(&ui, &m, now, &t1))
        return 0;

    if (t1.full)
    {
        fprintf(stderr, "UI DIRTY FULL on speed-only update\n");
        return 0;
    }
    if (t1.dirty_count == 0u || t1.dirty_count > UI_MAX_DIRTY)
    {
        fprintf(stderr, "UI DIRTY COUNT out of range (%u > %u)\n", t1.dirty_count, UI_MAX_DIRTY);
        return 0;
    }
    if (t1.render_ms > UI_TICK_MS)
    {
        fprintf(stderr, "UI RENDER BUDGET EXCEEDED dashboard dt=%u\n", t1.render_ms);
        return 0;
    }
    return 1;
}

static int test_ui_registry_pages(void)
{
    ui_state_t ui;
    ui_init(&ui);
    ui_model_t m = {0};
    m.speed_dmph = 123;
    m.power_w = 420;
    m.batt_dV = 360;
    m.batt_dA = -12;
    m.cadence_rpm = 88;
    m.soc_pct = 75;
    m.units = 0;
    m.theme = UI_THEME_DAY;
    m.mode = 0;

    uint32_t now = 0;
    ui_trace_t trace;
    uint8_t count = ui_registry_layout_count();
    if (!count)
        return 0;

    for (uint8_t i = 0; i < count; ++i)
    {
        m.page = ui_registry_layout_get(i);
        now += UI_TICK_MS;
        if (!ui_tick(&ui, &m, now, &trace))
            return 0;
        if (trace.hash == 0u)
            return 0;
    }
    return 1;
}

static int test_trip_summary_hash(void)
{
    ui_state_t ui;
    ui_init(&ui);
    ui_model_t m = {0};
    m.page = UI_PAGE_TRIP;
    m.trip_distance_mm = 1609340u * 8u; /* 8.0 mi */
    m.trip_energy_mwh = 456700u;       /* 456.7 Wh */
    m.trip_max_speed_dmph = 256u;      /* 25.6 mph */
    m.trip_avg_speed_dmph = 180u;      /* 18.0 mph */
    m.trip_moving_ms = 5400u * 1000u;  /* 1h30m */
    m.trip_assist_ms = 3600u * 1000u;  /* 1h */
    m.trip_gear_ms = 1800u * 1000u;    /* 30m */
    m.virtual_gear = 3u;
    m.units = 0u;
    m.theme = UI_THEME_DAY;

    uint32_t now = 0;
    ui_trace_t t0;
    ui_trace_t t1;
    ui_trace_t t2;
    ui_trace_t t3;

    now += UI_TICK_MS;
    if (!ui_tick(&ui, &m, now, &t0))
        return 0;

    now += UI_TICK_MS;
    if (!ui_tick(&ui, &m, now, &t1))
        return 0;
    if (t0.hash != t1.hash)
        return 0;
    if (t1.dirty_count != 0u)
        return 0;

    m.trip_moving_ms += 60000u;
    now += UI_TICK_MS;
    if (!ui_tick(&ui, &m, now, &t2))
        return 0;
    if (t1.hash == t2.hash)
        return 0;

    m.units = 1u;
    now += UI_TICK_MS;
    if (!ui_tick(&ui, &m, now, &t3))
        return 0;
    if (t2.hash == t3.hash)
        return 0;

    return 1;
}

static int test_settings_highlight(void)
{
    ui_state_t ui;
    ui_init(&ui);
    ui_model_t m = {0};
    m.page = UI_PAGE_SETTINGS;
    m.units = 0;
    m.theme = UI_THEME_DAY;
    m.mode = 0;

    uint32_t now = 0;
    ui_trace_t t0;
    ui_trace_t t1;

    m.settings_index = 0;
    now += UI_TICK_MS;
    if (!ui_tick(&ui, &m, now, &t0))
        return 0;

    m.settings_index = 1;
    now += UI_TICK_MS;
    if (!ui_tick(&ui, &m, now, &t1))
        return 0;

    if (t0.hash == t1.hash)
        return 0;
    return 1;
}

static int test_tune_highlight(void)
{
    ui_state_t ui;
    ui_init(&ui);
    ui_model_t m = {0};
    m.page = UI_PAGE_TUNE;
    m.tune_cap_current_dA = 200;
    m.tune_ramp_wps = 200;
    m.tune_boost_s = 6;

    uint32_t now = 0;
    ui_trace_t t0;
    ui_trace_t t1;

    m.tune_index = 0;
    now += UI_TICK_MS;
    if (!ui_tick(&ui, &m, now, &t0))
        return 0;

    m.tune_index = 1;
    now += UI_TICK_MS;
    if (!ui_tick(&ui, &m, now, &t1))
        return 0;

    if (t0.hash == t1.hash)
        return 0;
    return 1;
}

int main(void)
{
    if (!test_engineer_trace())
        return 1;
    if (!test_ui_registry_pages())
        return 1;
    if (!test_ui_hash_determinism())
        return 1;
    if (!test_dashboard_dirty_budget())
        return 1;
    if (!test_trip_summary_hash())
        return 1;
    if (!test_settings_highlight())
        return 1;
    if (!test_tune_highlight())
        return 1;
    if (!test_draw_format_value())
        return 1;
    if (!test_draw_dither_pick())
        return 1;
    if (!test_round_rect_solid())
        return 1;
    if (!test_round_rect_dither_alt())
        return 1;
    if (!test_big_digit_variation())
        return 1;
    if (!test_battery_icon_soc())
        return 1;
    if (!test_warning_icon_pixels())
        return 1;
    if (!test_ring_arc_full())
        return 1;
    if (!test_font_width_widest_chars())
        return 1;
    if (!test_grid_layout_system())
        return 1;
    if (!test_layout_density())
        return 1;
    printf("UI ENGINEER TRACE PASS\n");
    return 0;
}
