#ifndef OPEN_FIRMWARE_UI_H
#define OPEN_FIRMWARE_UI_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define UI_TICK_MS 200u
#define UI_GRAPH_SAMPLES 150u /* 30s window @ 5 Hz (UI_TICK_MS=200) */
#define UI_GRAPH_CH_SPEED 0u
#define UI_GRAPH_CH_POWER 1u
#define UI_GRAPH_CH_VOLT  2u
#define UI_GRAPH_CH_CAD   3u
#define UI_MAX_DIRTY 12u

#define UI_FOCUS_METRIC_SPEED 0u
#define UI_FOCUS_METRIC_POWER 1u

typedef enum {
    UI_PAGE_DASHBOARD = 0,
    UI_PAGE_ENGINEER_RAW = 1,
    UI_PAGE_ENGINEER_POWER = 2,
    UI_PAGE_FOCUS = 3,
    UI_PAGE_GRAPHS = 4,
    UI_PAGE_TRIP = 5,
    UI_PAGE_PROFILES = 6,
    UI_PAGE_SETTINGS = 7,
    UI_PAGE_CRUISE = 8,
    UI_PAGE_BATTERY = 9,
    UI_PAGE_THERMAL = 10,
    UI_PAGE_DIAGNOSTICS = 11,
    UI_PAGE_BUS = 12,
    UI_PAGE_CAPTURE = 13,
    UI_PAGE_ALERTS = 14,
    UI_PAGE_TUNE = 15,
    UI_PAGE_AMBIENT = 16,
    UI_PAGE_ABOUT = 17,
} ui_page_t;


#define UI_PALETTE_COLORS 8u
#define UI_THEME_DAY 0u
#define UI_THEME_NIGHT 1u
#define UI_THEME_HIGH_CONTRAST 2u
#define UI_THEME_COLORBLIND 3u
#define UI_THEME_COUNT 4u
#define UI_SETTINGS_ITEM_WIZARD 0u
#define UI_SETTINGS_ITEM_UNITS 1u
#define UI_SETTINGS_ITEM_BUTTON_MAP 2u
#define UI_SETTINGS_ITEM_THEME 3u
#define UI_SETTINGS_ITEM_MODE 4u
#define UI_SETTINGS_ITEM_PIN 5u
#define UI_SETTINGS_ITEM_COUNT 6u

typedef enum {
    UI_COLOR_BG = 0,
    UI_COLOR_PANEL = 1,
    UI_COLOR_TEXT = 2,
    UI_COLOR_MUTED = 3,
    UI_COLOR_ACCENT = 4,
    UI_COLOR_WARN = 5,
    UI_COLOR_DANGER = 6,
    UI_COLOR_OK = 7,
} ui_color_id_t;

typedef struct {
    uint16_t colors[UI_PALETTE_COLORS];
} ui_palette_t;

#define UI_PAGE_BUTTON_RAW   0x04u
#define UI_PAGE_BUTTON_POWER 0x08u

#define UI_PROFILE_FOCUS_LIST 0u
#define UI_PROFILE_FOCUS_GEAR_MIN 1u
#define UI_PROFILE_FOCUS_GEAR_MAX 2u
#define UI_PROFILE_FOCUS_GEAR_SHAPE 3u
#define UI_PROFILE_FOCUS_COUNT 4u

typedef struct {
    uint16_t x;
    uint16_t y;
    uint16_t w;
    uint16_t h;
} ui_rect_t;

typedef struct {
    uint8_t radius;
    uint8_t border_thick;
    int8_t shadow_dx;
    int8_t shadow_dy;
    uint16_t fill;
    uint16_t border;
    uint16_t shadow;
    uint8_t flags;
} ui_panel_style_t;

#define UI_PANEL_FLAG_DITHER 0x01u

typedef struct {
    uint8_t page;
    uint16_t speed_dmph;
    uint16_t rpm;
    uint16_t torque_raw;
    uint8_t assist_mode;
    uint8_t virtual_gear;
    uint8_t soc_pct;
    uint8_t err;
    int16_t batt_dV;
    int16_t batt_dA;
    int16_t phase_dA;
    int16_t sag_margin_dV;
    uint16_t thermal_state;
    int16_t ctrl_temp_dC;
    uint16_t cadence_rpm;
    uint8_t throttle_pct;
    uint8_t brake;
    uint8_t buttons;
    uint16_t power_w;
    uint16_t limit_power_w;
    uint32_t trip_distance_mm;
    uint32_t trip_energy_mwh;
    uint16_t trip_max_speed_dmph;
    uint16_t trip_avg_speed_dmph;
    uint32_t trip_moving_ms;
    uint32_t trip_assist_ms;
    uint32_t trip_gear_ms;
    uint8_t units; /* 0=imperial, 1=metric */
    uint8_t theme;
    uint8_t mode;  /* 0=street/legal, 1=private */
    uint8_t limit_reason;
    uint8_t drive_mode; /* 0=auto,1=manual current,2=manual power,3=sport */
    uint8_t boost_seconds;
    uint16_t range_est_d10;
    uint8_t range_confidence;
    uint8_t cruise_resume_available;
    uint8_t cruise_resume_reason;
    uint8_t regen_supported;
    uint8_t regen_level;
    uint8_t regen_brake_level;
    uint16_t regen_cmd_power_w;
    uint16_t regen_cmd_current_dA;
    uint8_t walk_state; /* 0=off, 1=active, 2=cancelled, 3=disabled */
    uint8_t settings_index;
    uint8_t focus_metric;
    uint8_t button_map;
    uint16_t pin_code;
    uint8_t capture_enabled;
    uint16_t capture_count;
    uint8_t alert_ack_active;
    uint16_t alert_count;
    uint8_t bus_last_id;
    uint8_t bus_last_len;
    uint8_t bus_last_opcode;
    uint16_t bus_last_dt_ms;
    uint8_t bus_count;
    uint8_t profile_id;
    uint8_t tune_index;
    uint16_t tune_cap_current_dA;
    uint16_t tune_ramp_wps;
    uint8_t tune_boost_s;
    uint8_t cruise_mode;
    uint16_t cruise_set_dmph;
    uint16_t cruise_set_power_w;
    uint8_t alert_entries;
    uint8_t alert_type[3];
    uint8_t alert_flags[3];
    uint16_t alert_age_s[3];
    uint16_t alert_dist_d10[3];
    uint8_t graph_channel;
    uint8_t graph_window_s;
    uint8_t graph_sample_hz;
    uint8_t bus_diff;
    uint8_t bus_changed_only;
    uint8_t bus_entries;
    uint8_t bus_filter_id_active;
    uint8_t bus_filter_opcode_active;
    uint8_t bus_filter_id;
    uint8_t bus_filter_opcode;
    uint8_t bus_list_id[6];
    uint8_t bus_list_op[6];
    uint8_t bus_list_len[6];
    uint16_t bus_list_dt_ms[6];
    uint8_t alert_selected;
    uint8_t alert_ack_mask;
    uint8_t profile_select;
    uint8_t profile_focus;
    uint8_t gear_count;
    uint8_t gear_shape;
    uint16_t gear_min_pct;
    uint16_t gear_max_pct;
} ui_model_t;

typedef struct ui_render_ctx ui_render_ctx_t;
typedef struct ui_dirty ui_dirty_t;

typedef void (*ui_render_full_fn)(ui_render_ctx_t *ctx, const ui_model_t *model,
                                  uint16_t trip_distance_d10, uint16_t trip_wh_per_unit_d10);
typedef void (*ui_render_partial_fn)(ui_render_ctx_t *ctx, const ui_model_t *model,
                                     uint16_t trip_distance_d10, uint16_t trip_wh_per_unit_d10,
                                     const ui_dirty_t *dirty);
typedef void (*ui_dirty_fn)(ui_dirty_t *dirty, const ui_model_t *model, const ui_model_t *prev);

#define UI_SCREEN_FLAG_PARTIAL 0x01u

typedef struct {
    uint8_t id;
    uint8_t flags;
    const char *name;
    ui_render_full_fn render_full;
    ui_render_partial_fn render_partial;
    ui_dirty_fn dirty_fn;
} ui_screen_def_t;

typedef struct {
    uint32_t hash;
    uint16_t dirty_count;
    uint16_t draw_ops;
    uint16_t render_ms;
    uint8_t full;
    uint8_t page;
    uint16_t trip_distance_d10;
    uint16_t trip_wh_per_unit_d10;
} ui_trace_t;

typedef struct {
    ui_model_t prev;
    uint32_t last_tick_ms;
    uint32_t hash;
    uint16_t draw_ops;
    uint8_t prev_valid;
    uint8_t graph_head;
    uint8_t graph_count;
    uint8_t graph_channel;
    uint16_t graph_samples[UI_GRAPH_SAMPLES];
} ui_state_t;

void ui_init(ui_state_t *ui);
bool ui_tick(ui_state_t *ui, const ui_model_t *model, uint32_t now_ms, ui_trace_t *trace);

uint8_t ui_page_from_buttons(uint8_t short_press, uint8_t long_press, uint8_t current_page);
const char *ui_page_name(uint8_t page);
uint8_t ui_registry_count(void);
uint8_t ui_registry_layout_count(void);
uint8_t ui_registry_layout_get(uint8_t index);
uint8_t ui_registry_index(uint8_t page);
size_t ui_registry_format_trace(char *out, size_t len);

void ui_dirty_add(ui_dirty_t *dirty, ui_rect_t rect);
void ui_dirty_full(ui_dirty_t *dirty);

void ui_draw_round_rect(ui_render_ctx_t *ctx, ui_rect_t rect, uint16_t color, uint8_t radius);
void ui_draw_rect(ui_render_ctx_t *ctx, ui_rect_t rect, uint16_t color);
void ui_draw_text(ui_render_ctx_t *ctx, uint16_t x, uint16_t y, const char *text, uint16_t fg, uint16_t bg);
void ui_draw_value(ui_render_ctx_t *ctx, uint16_t x, uint16_t y, const char *label,
                   int32_t value, uint16_t fg, uint16_t bg);
void ui_draw_big_digit(ui_render_ctx_t *ctx, uint16_t x, uint16_t y, uint8_t digit,
                       uint8_t scale, uint16_t color);
void ui_draw_battery_icon(ui_render_ctx_t *ctx, ui_rect_t rect, uint8_t soc, uint16_t color, uint16_t bg);
void ui_draw_warning_icon(ui_render_ctx_t *ctx, uint16_t x, uint16_t y, uint16_t color);
void ui_draw_panel(ui_render_ctx_t *ctx, ui_rect_t rect, const ui_panel_style_t *style);
void ui_draw_ring_arc(ui_render_ctx_t *ctx, ui_rect_t clip,
                      int16_t cx, int16_t cy, uint16_t outer_r, uint16_t thickness,
                      int16_t start_deg_cw, uint16_t sweep_deg_cw,
                      uint16_t fg, uint16_t bg);
void ui_draw_ring_gauge(ui_render_ctx_t *ctx, ui_rect_t clip,
                        int16_t cx, int16_t cy, uint16_t outer_r, uint16_t thickness,
                        int16_t start_deg_cw, uint16_t sweep_deg_cw, uint16_t active_sweep_deg_cw,
                        uint16_t fg_active, uint16_t fg_inactive, uint16_t bg);

const ui_palette_t *ui_theme_palette(uint8_t theme_id);
uint8_t ui_theme_normalize(uint8_t theme_id);
size_t ui_format_engineer_trace(char *out, size_t len, const ui_model_t *model);
size_t ui_format_dashboard_trace(char *out, size_t len, const ui_model_t *model,
                                  const ui_trace_t *trace, uint32_t now_ms);

#endif
