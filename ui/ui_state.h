#ifndef OPEN_FIRMWARE_UI_STATE_H
#define OPEN_FIRMWARE_UI_STATE_H

#include <stdint.h>
#include "ui.h"

/* UI runtime state (defined in ui_state.c) */
extern ui_state_t g_ui;
extern ui_model_t g_ui_model;

extern ui_page_t g_ui_page;
extern ui_page_t g_ui_focus_prev_page;

extern uint8_t g_ui_settings_index;
extern uint8_t g_ui_tune_index;
extern uint8_t g_ui_graph_channel;
extern uint8_t g_ui_graph_window_idx;
extern uint8_t g_ui_bus_offset;
extern uint8_t g_ui_profile_select;
extern uint8_t g_ui_profile_focus;
extern uint8_t g_ui_alert_index;
extern uint8_t g_ui_alert_ack_mask;
extern uint32_t g_ui_alert_last_seq;
extern uint8_t g_alert_ack_active;
extern uint32_t g_alert_ack_until_ms;

#endif /* OPEN_FIRMWARE_UI_STATE_H */
