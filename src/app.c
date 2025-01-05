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
#include "src/motor/motor_isr.h" /* motor_isr_get_stats */
#include "src/motor/motor_cmd.h"
#include "src/kernel/event_queue.h"
#include "src/system_control.h"    /* reboot_to_bootloader, reboot_to_app, watchdog_tick */
#include "storage/logs.h"          /* event_log_meta_t */
#include "storage/boot_stage.h"    /* boot_stage_log */
#include "platform/time.h"         /* platform_time_poll_1ms */
#include "platform/cpu.h"          /* wfi */
#include "platform/hw.h"           /* UART1_BASE */
#include "drivers/uart.h"          /* uart_write */
#include "drivers/spi_flash.h"     /* spi_flash_set_bootloader_mode_flag */

/* Alert acknowledgement timeout (ms) */
#define UI_ALERT_ACK_MS 5000u

/*
 * Extern declarations for globals defined in main.c
 * These are the canonical instances; types come from headers above.
 */
extern volatile uint32_t g_ms;
extern uint32_t g_last_profile_switch_ms;
extern uint8_t g_last_brake_state;
void recompute_outputs(void);

static void apply_gear_buttons(void)
{
    uint8_t prev = g_active_vgear;
    uint8_t rising = (uint8_t)(g_button_short_press & (BUTTON_GEAR_UP_MASK | BUTTON_GEAR_DOWN_MASK));
    if (rising & BUTTON_GEAR_UP_MASK)
    {
        if (g_active_vgear < g_vgears.count)
            g_active_vgear++;
    }
    if (rising & BUTTON_GEAR_DOWN_MASK)
    {
        if (g_active_vgear > 1)
            g_active_vgear--;
    }
    if (g_active_vgear != prev)
        shengyi_request_update(0u);
}

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

static void handle_motor_event(const event_t *evt, void *ctx)
{
    (void)ctx;
    motor_cmd_process(evt);
}

void app_process_events(void)
{
    poll_uart_rx_ports();
    buttons_tick();
    extern event_queue_t g_motor_events;
    event_queue_drain(&g_motor_events, handle_motor_event, NULL);
}

void app_apply_inputs(void)
{
    graph_on_input_all();

    if (g_ui_page == UI_PAGE_SETTINGS)
    {
        uint8_t press = g_button_short_press;
        if (press & BUTTON_GEAR_UP_MASK)
        {
            if (g_ui_settings_index == 0u)
                g_ui_settings_index = (uint8_t)(UI_SETTINGS_ITEM_COUNT - 1u);
            else
                g_ui_settings_index--;
        }
        if (press & BUTTON_GEAR_DOWN_MASK)
            g_ui_settings_index = (uint8_t)((g_ui_settings_index + 1u) % UI_SETTINGS_ITEM_COUNT);
        if (press & UI_PAGE_BUTTON_RAW)
        {
            switch (g_ui_settings_index)
            {
            case UI_SETTINGS_ITEM_WIZARD:
                wizard_start();
                break;
            case UI_SETTINGS_ITEM_UNITS:
                g_config_active.units = g_config_active.units ? 0u : 1u;
                config_persist_active();
                break;
            case UI_SETTINGS_ITEM_BUTTON_MAP:
                g_config_active.button_map = (uint8_t)((g_config_active.button_map + 1u) % (BUTTON_MAP_MAX + 1u));
                config_persist_active();
                break;
            case UI_SETTINGS_ITEM_THEME:
                g_config_active.theme = (uint8_t)((g_config_active.theme + 1u) % UI_THEME_COUNT);
                config_persist_active();
                break;
            case UI_SETTINGS_ITEM_MODE:
                g_config_active.mode = (g_config_active.mode == MODE_PRIVATE) ? MODE_STREET : MODE_PRIVATE;
                config_persist_active();
                break;
            case UI_SETTINGS_ITEM_PIN:
                break;
            default:
                break;
            }
        }
    }

    if (g_ui_page == UI_PAGE_GRAPHS)
    {
        if (g_button_short_press & UI_PAGE_BUTTON_RAW)
            g_ui_graph_channel = (uint8_t)((g_ui_graph_channel + 1u) % 4u);
        if (g_button_short_press & BUTTON_GEAR_UP_MASK)
            g_ui_graph_window_idx = (uint8_t)((g_ui_graph_window_idx + 1u) % 3u);
        if (g_button_short_press & BUTTON_GEAR_DOWN_MASK)
            g_ui_graph_window_idx = (uint8_t)((g_ui_graph_window_idx + 2u) % 3u);
    }

    if (g_ui_page == UI_PAGE_PROFILES)
    {
        uint8_t press = g_button_short_press;
        uint8_t long_press = g_button_long_press;
        uint8_t confirm = (press & UI_PAGE_BUTTON_RAW) ? 1u : 0u;
        uint8_t up = (press & BUTTON_GEAR_UP_MASK) ? 1u : 0u;
        uint8_t down = (press & BUTTON_GEAR_DOWN_MASK) ? 1u : 0u;
        uint8_t long_up = (long_press & BUTTON_GEAR_UP_MASK) ? 1u : 0u;
        uint8_t long_down = (long_press & BUTTON_GEAR_DOWN_MASK) ? 1u : 0u;
        uint8_t long_cruise = (long_press & UI_PAGE_BUTTON_RAW) ? 1u : 0u;

        if (g_config_active.flags & CFG_FLAG_QA_PROFILE)
            long_up = 0u;
        if (g_config_active.flags & CFG_FLAG_QA_CAPTURE)
            long_down = 0u;
        if (g_config_active.flags & CFG_FLAG_QA_CRUISE)
            long_cruise = 0u;

        if (g_ui_profile_focus >= UI_PROFILE_FOCUS_COUNT)
            g_ui_profile_focus = UI_PROFILE_FOCUS_LIST;
        if (g_ui_profile_select >= PROFILE_COUNT)
            g_ui_profile_select = g_active_profile_id;

        if (g_ui_profile_focus == UI_PROFILE_FOCUS_LIST)
        {
            if (up)
            {
                if (g_ui_profile_select == 0u)
                    g_ui_profile_select = (uint8_t)(PROFILE_COUNT - 1u);
                else
                    g_ui_profile_select--;
            }
            if (down)
                g_ui_profile_select = (uint8_t)((g_ui_profile_select + 1u) % PROFILE_COUNT);
            if (confirm)
                set_active_profile(g_ui_profile_select, 1);
            if (long_cruise)
                g_ui_profile_focus = UI_PROFILE_FOCUS_GEAR_MIN;
        }
        else
        {
            int dir = 0;
            int dir_fast = 0;
            if (up)
                dir = 1;
            else if (down)
                dir = -1;
            if (long_up)
                dir_fast = 1;
            else if (long_down)
                dir_fast = -1;

            if (g_ui_profile_focus == UI_PROFILE_FOCUS_GEAR_MIN)
            {
                if (dir)
                    vgear_adjust_min(dir, VGEAR_UI_STEP_Q15);
                if (dir_fast)
                    vgear_adjust_min(dir_fast, VGEAR_UI_STEP_FAST_Q15);
            }
            else if (g_ui_profile_focus == UI_PROFILE_FOCUS_GEAR_MAX)
            {
                if (dir)
                    vgear_adjust_max(dir, VGEAR_UI_STEP_Q15);
                if (dir_fast)
                    vgear_adjust_max(dir_fast, VGEAR_UI_STEP_FAST_Q15);
            }
            else
            {
                if (dir || dir_fast)
                {
                    g_vgears.shape = (g_vgears.shape == VGEAR_SHAPE_EXP) ? VGEAR_SHAPE_LINEAR : VGEAR_SHAPE_EXP;
                    vgear_generate_scales(&g_vgears);
                }
            }

            if (confirm)
            {
                g_ui_profile_focus = (uint8_t)(g_ui_profile_focus + 1u);
                if (g_ui_profile_focus >= UI_PROFILE_FOCUS_COUNT)
                    g_ui_profile_focus = UI_PROFILE_FOCUS_LIST;
            }
            if (long_cruise)
                g_ui_profile_focus = UI_PROFILE_FOCUS_LIST;
        }
    }

    if (g_ui_page == UI_PAGE_TUNE)
    {
        uint8_t press = g_button_short_press;
        if (press & UI_PAGE_BUTTON_RAW)
            g_ui_tune_index = (uint8_t)((g_ui_tune_index + 1u) % 3u);
        if (press & (BUTTON_GEAR_UP_MASK | BUTTON_GEAR_DOWN_MASK))
        {
            int dir = (press & BUTTON_GEAR_UP_MASK) ? 1 : -1;
            if (g_ui_tune_index == 0u)
            {
                int32_t v = (int32_t)g_config_active.cap_current_dA + dir * 10;
                int32_t max_current = (g_config_active.mode == MODE_STREET) ? (int32_t)STREET_MAX_CURRENT_DA : 300;
                if (v < 50)
                    v = 50;
                if (v > max_current)
                    v = max_current;
                g_config_active.cap_current_dA = (uint16_t)v;
            }
            else if (g_ui_tune_index == 1u)
            {
                int32_t v = (int32_t)g_config_active.soft_start_ramp_wps + dir * 50;
                if (v <= 0)
                    v = 0;
                else if (v < (int32_t)SOFT_START_RAMP_MIN_WPS)
                    v = SOFT_START_RAMP_MIN_WPS;
                if (v > (int32_t)SOFT_START_RAMP_MAX_WPS)
                    v = SOFT_START_RAMP_MAX_WPS;
                g_config_active.soft_start_ramp_wps = (uint16_t)v;
            }
            else
            {
                int32_t v = (int32_t)g_config_active.boost_budget_ms + dir * 1000;
                if (v < 0)
                    v = 0;
                if (v > (int32_t)BOOST_BUDGET_MAX_MS)
                    v = BOOST_BUDGET_MAX_MS;
                g_config_active.boost_budget_ms = (uint16_t)v;
            }
            config_persist_active();
        }
    }

    if (g_ui_page == UI_PAGE_CAPTURE)
    {
        if (g_button_short_press & UI_PAGE_BUTTON_RAW)
        {
            uint8_t enable = bus_capture_get_enabled() ? 0u : 1u;
            bus_capture_set_enabled(enable, enable);
        }
    }

    if (g_ui_page == UI_PAGE_ALERTS)
    {
        if (g_button_short_press & BUTTON_GEAR_UP_MASK)
        {
            if (g_ui_alert_index == 0u)
                g_ui_alert_index = 2u;
            else
                g_ui_alert_index--;
        }
        if (g_button_short_press & BUTTON_GEAR_DOWN_MASK)
            g_ui_alert_index = (uint8_t)((g_ui_alert_index + 1u) % 3u);
        if (g_button_short_press & UI_PAGE_BUTTON_RAW)
            g_ui_alert_ack_mask ^= (uint8_t)(1u << g_ui_alert_index);
        if (g_button_long_press & UI_PAGE_BUTTON_RAW)
        {
            g_alert_ack_active = 1u;
            g_alert_ack_until_ms = g_ms + UI_ALERT_ACK_MS;
        }
    }

    if (g_ui_page == UI_PAGE_BUS)
    {
        bus_ui_state_t state;
        bus_ui_get_state(&state);
        bus_ui_entry_t last_entry;
        uint8_t have_last = bus_ui_get_last(&last_entry);

        uint8_t changed_only = state.changed_only ? 1u : 0u;
        uint8_t diff_enabled = state.diff_enabled ? 1u : 0u;
        uint8_t filter_id = state.filter_id ? 1u : 0u;
        uint8_t filter_opcode = state.filter_opcode ? 1u : 0u;
        uint8_t filter_bus_id = state.filter_bus_id;
        uint8_t filter_opcode_val = state.filter_opcode_val;
        uint8_t apply_reset = 0u;

        if (g_button_short_press & BUTTON_GEAR_UP_MASK)
        {
            if (g_ui_bus_offset > 0u)
                g_ui_bus_offset--;
        }
        if (g_button_short_press & BUTTON_GEAR_DOWN_MASK)
        {
            if (g_ui_bus_offset < 0xFFu)
                g_ui_bus_offset++;
        }
        if (g_button_short_press & WALK_BUTTON_MASK)
            changed_only = changed_only ? 0u : 1u;
        if (g_button_short_press & UI_PAGE_BUTTON_RAW)
            diff_enabled = diff_enabled ? 0u : 1u;
        if (g_button_long_press & BUTTON_GEAR_UP_MASK)
        {
            filter_id = filter_id ? 0u : 1u;
            if (have_last)
                filter_bus_id = last_entry.bus_id;
            apply_reset = 1u;
        }
        if (g_button_long_press & BUTTON_GEAR_DOWN_MASK)
        {
            filter_opcode = filter_opcode ? 0u : 1u;
            if (have_last)
                filter_opcode_val = last_entry.len ? last_entry.data[0] : 0u;
            apply_reset = 1u;
        }
        if (g_button_long_press & UI_PAGE_BUTTON_RAW)
            bus_ui_reset();
        {
            uint8_t flags = BUS_UI_FLAG_ENABLE;
            if (filter_id)
                flags |= BUS_UI_FLAG_FILTER_ID;
            if (filter_opcode)
                flags |= BUS_UI_FLAG_FILTER_OPCODE;
            if (diff_enabled)
                flags |= BUS_UI_FLAG_DIFF;
            if (changed_only)
                flags |= BUS_UI_FLAG_CHANGED_ONLY;
            if (apply_reset)
                flags |= BUS_UI_FLAG_RESET;
            bus_ui_set_control(flags, filter_bus_id, filter_opcode_val);
            if (apply_reset)
                g_ui_bus_offset = 0u;
        }
    }

    if (g_ui_page == UI_PAGE_CRUISE)
    {
        int dir = 0;
        if (g_button_short_press & BUTTON_GEAR_UP_MASK)
            dir = 1;
        else if (g_button_short_press & BUTTON_GEAR_DOWN_MASK)
            dir = -1;
        if (dir && g_cruise.mode == CRUISE_SPEED)
        {
            int32_t v = (int32_t)g_cruise.set_speed_dmph + dir * 5;
            if (v < (int32_t)CRUISE_MIN_SPEED_DMPH)
                v = CRUISE_MIN_SPEED_DMPH;
            if (v > (int32_t)STREET_MAX_SPEED_DMPH)
                v = STREET_MAX_SPEED_DMPH;
            g_cruise.set_speed_dmph = (uint16_t)v;
        }
        if (dir && g_cruise.mode == CRUISE_POWER)
        {
            int32_t v = (int32_t)g_cruise.set_power_w + dir * 20;
            if (v < 0)
                v = 0;
            if (v > (int32_t)MANUAL_POWER_MAX_W)
                v = MANUAL_POWER_MAX_W;
            g_cruise.set_power_w = (uint16_t)v;
        }
    }

    if (g_alert_ack_active)
    {
        if ((int32_t)(g_ms - g_alert_ack_until_ms) >= 0)
            g_alert_ack_active = 0u;
        if (!g_motor.err && g_power_policy.last_reason == LIMIT_REASON_USER)
            g_alert_ack_active = 0u;
    }

    if (g_event_meta.seq != g_ui_alert_last_seq)
    {
        g_ui_alert_last_seq = g_event_meta.seq;
        g_ui_alert_ack_mask = 0u;
        g_ui_alert_index = 0u;
    }

    /* Track brake edge for logging after outputs are updated. */
    g_brake_edge = (g_inputs.brake && !g_last_brake_state) ? 1u : 0u;

    /* Profile quick-switch via buttons (low 2 bits). */
    uint8_t requested_profile = (uint8_t)(g_inputs.buttons & 0x03u);
    if (requested_profile < PROFILE_COUNT && requested_profile != g_active_profile_id)
    {
        /* debounce ~100 ms to avoid chatter while remaining quick (<300 ms) */
        if (g_last_profile_switch_ms == 0u || (g_ms - g_last_profile_switch_ms) > 100u)
            set_active_profile(requested_profile, 1);
    }

    /* Virtual gear up/down: bit4=up, bit5=down (edge-trigger). */
    apply_gear_buttons();
    if (g_active_vgear == 0 || g_active_vgear > g_vgears.count)
        g_active_vgear = 1;

    recompute_outputs();

    /* Log brake activation after outputs are zeroed so snapshots reflect the cancel. */
    if (g_brake_edge)
        event_log_append(EVT_BRAKE, 0);
    g_last_brake_state = g_inputs.brake ? 1u : 0u;

    trip_update(g_inputs.speed_dmph, g_inputs.power_w, g_outputs.assist_mode,
                g_outputs.virtual_gear, g_outputs.profile_id);
    {
        uint16_t sample_power = g_inputs.power_w ? g_inputs.power_w : g_outputs.cmd_power_w;
        range_update(g_inputs.speed_dmph, sample_power, g_motor.soc_pct);
    }
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
    motor_isr_stats_t link_stats;
    motor_isr_get_stats(&link_stats);
    g_ui_model.link_timeouts = (link_stats.timeouts > 0xFFFFu) ? 0xFFFFu : (uint16_t)link_stats.timeouts;
    g_ui_model.link_rx_errors = (link_stats.rx_errors > 0xFFFFu) ? 0xFFFFu : (uint16_t)link_stats.rx_errors;
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
    ui_trace_t trace;
    ui_trace_t *trace_ptr = (g_debug_uart_mask & DEBUG_UART_TRACE_UI) ? &trace : NULL;
    if (trace_ptr) {
        trace = (ui_trace_t){0};
    }
    if (ui_tick(&g_ui, &g_ui_model, g_ms, trace_ptr)) {
        if (trace_ptr) {
            char line[180];
            size_t n = ui_format_dashboard_trace(line, sizeof(line), &g_ui_model, trace_ptr, g_ms);
            if (n > 0) {
                uart_write(UART1_BASE, (const uint8_t *)line, n);
            }
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
    boot_stage_log(0xB020);
    while (1) {
        app_process_time();
        app_process_events();
        app_apply_inputs();
        app_process_periodic();
        app_update_ui();
        app_housekeeping();
    }
}
