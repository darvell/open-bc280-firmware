/*
 * Application Orchestration Layer - Implementation
 *
 * This module extracts the main initialization and loop logic
 * from main.c into well-defined phases. The actual business logic
 * remains in main.c for now - this is just the orchestration layer.
 */

#include "app.h"
#include <stdint.h>
#include <stdbool.h>

/* Include canonical type definitions instead of duplicating */
#include "ui.h"                    /* ui_state_t, ui_model_t, ui_trace_t, UI_TICK_MS */
#include "ui_state.h"              /* UI navigation/runtime globals */
#include "app_state.h"             /* main loop shared state */
#include "src/motor/app_data.h"    /* motor_state_t, debug_inputs_t, debug_outputs_t */
#include "src/power/power.h"       /* power_policy_state_t */
#include "src/control/control.h"   /* cruise_state_t, regen_state_t, drive_state_t, boost_state_t, vgear_table_t */
#include "src/bus/bus.h"           /* bus_ui_entry_t, BUS_UI_VIEW_MAX */
#include "src/telemetry/trip.h"    /* trip_acc_t, trip_snapshot_t */
#include "src/telemetry/telemetry.h" /* g_graph_window_s */
#include "src/config/config.h"     /* config_t */
#include "src/profiles/profiles.h" /* g_active_profile_id */
#include "src/comm/comm.h"         /* poll_uart_rx_ports, send_state_frame_bin, print_status */
#include "src/input/input.h"       /* buttons_tick */
#include "src/motor/shengyi.h"   /* shengyi_periodic_send_tick */
#include "src/system_control.h"    /* reboot_to_bootloader, reboot_to_app, watchdog_tick */
#include "storage/logs.h"          /* event_log_meta_t */
#include "platform/time.h"         /* platform_time_poll_1ms */
#include "platform/cpu.h"          /* wfi */
#include "platform/hw.h"           /* UART1_BASE */
#include "drivers/uart.h"          /* uart_write */

/*
 * Extern declarations for globals defined in main.c
 * These are the canonical instances; types come from headers above.
 */
extern volatile uint32_t g_ms;

/* UI and main-loop state from ui_state.h + app_state.h */

/* HIST_GEAR_BINS from trip.h */

/*
 * ============================================================================
 * Application Functions - Implementation
 * ============================================================================
 */

void app_process_time(void)
{
    platform_time_poll_1ms();

    if (g_request_soft_reboot == 1) {
        reboot_to_bootloader();
    } else if (g_request_soft_reboot == 2) {
        reboot_to_app();
    }
}

void app_process_events(void)
{
    poll_uart_rx_ports();
    buttons_tick();
}

void app_process_periodic(void)
{
    if ((g_ms - g_last_print) >= 1000u) {
        g_last_print = g_ms;
        print_status();
    }

    if (g_stream_period_ms && ((g_ms - g_last_stream_ms) >= g_stream_period_ms)) {
        g_last_stream_ms = g_ms;
        send_state_frame_bin();
    }

    stream_log_tick();
    graph_tick();
    bus_replay_tick();
    shengyi_periodic_send_tick();
    g_brake_edge = 0;
}

void app_update_ui(void)
{
    if ((uint32_t)(g_ms - g_ui.last_tick_ms) < UI_TICK_MS) {
        return;
    }

    /* Populate UI model from global state */
    g_ui_model.page = (uint8_t)g_ui_page;
    g_ui_model.speed_dmph = g_motor.speed_dmph;
    g_ui_model.rpm = g_motor.rpm;
    g_ui_model.torque_raw = g_motor.torque_raw;
    g_ui_model.assist_mode = g_outputs.assist_mode;
    g_ui_model.virtual_gear = g_outputs.virtual_gear;
    g_ui_model.soc_pct = g_motor.soc_pct;
    g_ui_model.err = g_motor.err;
    g_ui_model.batt_dV = g_inputs.battery_dV;
    g_ui_model.batt_dA = g_inputs.battery_dA;
    g_ui_model.phase_dA = g_power_policy.i_phase_est_dA;
    g_ui_model.sag_margin_dV = g_power_policy.sag_margin_dV;
    g_ui_model.thermal_state = g_power_policy.thermal_state;
    g_ui_model.ctrl_temp_dC = g_inputs.ctrl_temp_dC;
    g_ui_model.cadence_rpm = g_inputs.cadence_rpm;
    g_ui_model.throttle_pct = g_inputs.throttle_pct;
    g_ui_model.brake = g_inputs.brake;
    g_ui_model.buttons = g_inputs.buttons;
    g_ui_model.power_w = g_outputs.cmd_power_w ? g_outputs.cmd_power_w : g_inputs.power_w;
    g_ui_model.limit_power_w = g_power_policy.p_final_w;
    /* Trip data from telemetry API */
    {
        const trip_acc_t *acc = trip_get_acc();
        trip_snapshot_t snap = {0};
        trip_get_current(&snap);

        g_ui_model.trip_distance_mm = acc->distance_mm;
        g_ui_model.trip_energy_mwh = acc->energy_mwh;
        g_ui_model.trip_max_speed_dmph = acc->max_speed_dmph;
        g_ui_model.trip_avg_speed_dmph = snap.avg_speed_dmph;
        g_ui_model.trip_moving_ms = acc->moving_ms;
        g_ui_model.trip_assist_ms = acc->assist_time_ms[1] + acc->assist_time_ms[2];

        /* Gear time */
        uint32_t gear_ms = 0u;
        if (g_ui_model.virtual_gear > 0u && g_ui_model.virtual_gear <= HIST_GEAR_BINS) {
            gear_ms = acc->gear_time_ms[g_ui_model.virtual_gear - 1u];
        }
        g_ui_model.trip_gear_ms = gear_ms;
    }

    g_ui_model.units = g_config_active.units;
    g_ui_model.theme = g_config_active.theme;
    g_ui_model.mode = g_config_active.mode;
    g_ui_model.limit_reason = g_power_policy.last_reason;
    g_ui_model.drive_mode = (uint8_t)g_drive.mode;
    g_ui_model.boost_seconds = (uint8_t)((g_boost.budget_ms + 500u) / 1000u);
    g_ui_model.range_est_d10 = g_range_est_d10;
    g_ui_model.range_confidence = g_range_confidence;
    g_ui_model.cruise_resume_available = g_cruise.resume_available;
    g_ui_model.cruise_resume_reason = g_cruise.resume_block_reason;
    g_ui_model.regen_supported = regen_capable() ? 1u : 0u;
    g_ui_model.regen_level = g_regen.level;
    g_ui_model.regen_brake_level = g_regen.brake_level;
    g_ui_model.regen_cmd_power_w = g_regen.cmd_power_w;
    g_ui_model.regen_cmd_current_dA = g_regen.cmd_current_dA;
    g_ui_model.walk_state = (uint8_t)g_walk_state;
    g_ui_model.settings_index = g_ui_settings_index;
    g_ui_model.focus_metric = (g_config_active.button_flags & 0x01) ? 1u : 0u;
    g_ui_model.button_map = g_config_active.button_map;
    g_ui_model.pin_code = g_config_active.pin_code;
    bus_ui_state_t bus_state;
    bus_ui_get_state(&bus_state);

    g_ui_model.capture_enabled = bus_capture_get_enabled() ? 1u : 0u;
    g_ui_model.capture_count = bus_capture_get_count();
    g_ui_model.alert_ack_active = g_alert_ack_active;
    g_ui_model.alert_count = (g_event_meta.count > 0xFFFFu) ? 0xFFFFu : (uint16_t)g_event_meta.count;
    g_ui_model.bus_count = bus_state.count;

    /* Bus last entry */
    bus_ui_entry_t last_entry;
    if (bus_ui_get_last(&last_entry)) {
        g_ui_model.bus_last_id = last_entry.bus_id;
        g_ui_model.bus_last_len = last_entry.len;
        g_ui_model.bus_last_dt_ms = last_entry.dt_ms;
        g_ui_model.bus_last_opcode = last_entry.len ? last_entry.data[0] : 0u;
    } else {
        g_ui_model.bus_last_id = 0u;
        g_ui_model.bus_last_len = 0u;
        g_ui_model.bus_last_dt_ms = 0u;
        g_ui_model.bus_last_opcode = 0u;
    }

    g_ui_model.profile_id = g_active_profile_id;
    g_ui_model.profile_select = g_ui_profile_select;
    g_ui_model.profile_focus = g_ui_profile_focus;
    g_ui_model.gear_count = g_vgears.count;
    g_ui_model.gear_shape = g_vgears.shape;
    g_ui_model.gear_min_pct = vgear_q15_to_pct(g_vgears.min_scale_q15);
    g_ui_model.gear_max_pct = vgear_q15_to_pct(g_vgears.max_scale_q15);
    g_ui_model.tune_index = g_ui_tune_index;
    g_ui_model.tune_cap_current_dA = g_config_active.cap_current_dA;
    g_ui_model.tune_ramp_wps = g_config_active.soft_start_ramp_wps;
    g_ui_model.tune_boost_s = (uint8_t)((g_config_active.boost_budget_ms + 500u) / 1000u);
    /* Track cruise mode changes for UI flash effect */
    {
        static uint8_t prev_cruise_mode = 0u;
        uint8_t new_mode = (uint8_t)g_cruise.mode;
        if (new_mode != prev_cruise_mode)
        {
            g_ui_model.cruise_change_ms = g_ms;
            prev_cruise_mode = new_mode;
        }
    }
    g_ui_model.cruise_mode = (uint8_t)g_cruise.mode;
    g_ui_model.cruise_set_dmph = g_cruise.set_speed_dmph;
    g_ui_model.cruise_set_power_w = g_cruise.set_power_w;
    g_ui_model.graph_channel = g_ui_graph_channel;
    g_ui_model.graph_window_s = (uint8_t)g_graph_window_s[g_ui_graph_window_idx];
    g_ui_model.graph_sample_hz = (uint8_t)(1000u / UI_TICK_MS);
    g_ui_model.bus_diff = bus_state.diff_enabled ? 1u : 0u;
    g_ui_model.bus_changed_only = bus_state.changed_only ? 1u : 0u;
    g_ui_model.bus_entries = 0u;
    g_ui_model.bus_filter_id_active = bus_state.filter_id ? 1u : 0u;
    g_ui_model.bus_filter_opcode_active = bus_state.filter_opcode ? 1u : 0u;
    g_ui_model.bus_filter_id = bus_state.filter_bus_id;
    g_ui_model.bus_filter_opcode = bus_state.filter_opcode_val;

    /* Initialize arrays */
    for (uint8_t i = 0; i < BUS_UI_VIEW_MAX; ++i) {
        g_ui_model.bus_list_id[i] = 0u;
        g_ui_model.bus_list_op[i] = 0u;
        g_ui_model.bus_list_len[i] = 0u;
        g_ui_model.bus_list_dt_ms[i] = 0u;
    }

    g_ui_model.alert_entries = 0u;
    for (uint8_t i = 0; i < 3u; ++i) {
        g_ui_model.alert_type[i] = 0u;
        g_ui_model.alert_flags[i] = 0u;
        g_ui_model.alert_age_s[i] = 0u;
        g_ui_model.alert_dist_d10[i] = 0u;
    }

    if (g_ui_model.alert_entries && g_ui_alert_index >= g_ui_model.alert_entries) {
        g_ui_alert_index = (uint8_t)(g_ui_model.alert_entries - 1u);
    }

    g_ui_model.alert_selected = g_ui_alert_index;
    g_ui_model.alert_ack_mask = g_ui_alert_ack_mask;

    /* Call UI tick to render and emit trace */
    ui_trace_t trace = {0};
    if (ui_tick(&g_ui, &g_ui_model, g_ms, &trace)) {
        char line[180];
        size_t n = ui_format_dashboard_trace(line, sizeof(line), &g_ui_model, &trace, g_ms);
        if (n > 0) {
            uart_write(UART1_BASE, (const uint8_t *)line, n);
        }
    }
}

void app_housekeeping(void)
{
    watchdog_tick();
    wfi();
}

void app_main_loop(void)
{
    while (1) {
        app_process_time();
        app_process_events();
        app_process_periodic();
        app_update_ui();
        app_housekeeping();
    }
}
