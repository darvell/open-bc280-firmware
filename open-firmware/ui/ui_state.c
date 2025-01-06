#include "ui_state.h"

ui_state_t g_ui;
ui_model_t g_ui_model;

ui_page_t g_ui_page = UI_PAGE_DASHBOARD;
ui_page_t g_ui_focus_prev_page = UI_PAGE_DASHBOARD;

uint8_t g_ui_settings_index;
uint8_t g_ui_tune_index;
uint8_t g_ui_graph_channel;
uint8_t g_ui_graph_window_idx;
uint8_t g_ui_bus_offset;
uint8_t g_ui_profile_select;
uint8_t g_ui_profile_focus;
uint8_t g_ui_alert_index;
uint8_t g_ui_alert_ack_mask;
uint32_t g_ui_alert_last_seq;
uint8_t g_alert_ack_active;
uint32_t g_alert_ack_until_ms;
