/*
 * Motor Command Processing Implementation
 *
 * Main loop handler for motor events from ISR queue.
 * Parses Shengyi DWG22 protocol responses and updates application state.
 */

#include "motor_cmd.h"
#include "motor_isr.h"
#include "shengyi.h"
#include "app_data.h"
#include "../control/control.h"
#include "../power/power.h"
#include "../telemetry/telemetry.h"
#include "../config/config.h"

#include <string.h>

#ifndef HOST_TEST
#include "../../platform/time.h"
#else
/* Host test stubs */
extern volatile uint32_t g_ms;
#endif

/*
 * Module state
 */
static struct {
    motor_status_cache_t status;    /* Last received status */
    motor_cmd_stats_t stats;        /* Event processing stats */

    /* Current command state */
    uint8_t assist_level;           /* Current assist level */
    bool light_on;                  /* Headlight state */
    bool walk_active;               /* Walk assist state */
    bool speed_over;                /* Speed limit exceeded */
    bool cmd_dirty;                 /* Command needs update */
} g_motor_cmd;

/*
 * Forward declarations
 */
static void motor_cmd_update_command(void);
static uint8_t motor_cmd_get_current_assist_mapped(void);
static bool motor_cmd_battery_low_flag(void);
static void motor_cmd_handle_status_0x52(const uint8_t *frame, uint8_t len, uint32_t now_ms);
static uint16_t motor_cmd_speed_dmph_from_raw(uint16_t speed_raw, uint16_t wheel_mm);
static int16_t motor_cmd_current_dA_from_raw(uint8_t current_raw);

/*
 * Initialize motor command processor
 */
void motor_cmd_init(void)
{
    memset(&g_motor_cmd, 0, sizeof(g_motor_cmd));
    g_motor_cmd.status.valid = false;
    g_motor_cmd.cmd_dirty = true;  /* Force initial command send */
}

/*
 * Process a motor event from the queue
 */
void motor_cmd_process(const event_t *evt)
{
    if (!evt || !EVENT_IS_MOTOR(*evt))
        return;

    g_motor_cmd.stats.events_processed++;
    g_motor_cmd.stats.last_event_ms = evt->timestamp;

    switch (evt->type) {
        case EVT_MOTOR_STATE: {
            /* Motor status update - parse the frame */
            g_motor_cmd.stats.state_updates++;

            /* The payload contains the opcode */
            uint8_t opcode = (uint8_t)(evt->payload16 & 0xFF);
            uint8_t seq = (uint8_t)(evt->payload16 >> 8);

            uint8_t frame[SHENGYI_MAX_FRAME_SIZE];
            uint8_t frame_len = 0;
            uint8_t frame_op = 0;
            uint8_t frame_seq = 0;
            if (motor_isr_copy_last_frame(frame, sizeof(frame), &frame_len, &frame_op, &frame_seq)) {
                if (frame_seq == seq && frame_op == opcode) {
                    if (opcode == SHENGYI_OPCODE_STATUS) {
                        motor_cmd_handle_status_0x52(frame, frame_len, evt->timestamp);
                    } else if (opcode == SHENGYI_OPCODE_CONFIG_53) {
                        if (shengyi_frame_validate(frame, frame_len, SHENGYI_OPCODE_CONFIG_53, 7u, NULL)) {
                            shengyi_notify_rx_opcode(opcode);
                            g_motor_cmd.cmd_dirty = true;
                            motor_cmd_update_command();
                        }
                    } else if (opcode == SHENGYI_OPCODE_STATUS_C0) {
                        const uint8_t *payload = NULL;
                        if (shengyi_frame_validate(frame, frame_len, SHENGYI_OPCODE_STATUS_C0, 52u, &payload)) {
                            uint8_t status = shengyi_apply_config_c0(payload, frame[3]) ? 1u : 0u;
                            uint8_t resp[16];
                            size_t rlen = shengyi_build_frame_0xC1(status, resp, sizeof(resp));
                            if (rlen)
                                motor_isr_queue_frame(resp, (uint8_t)rlen);
                            g_motor_cmd.cmd_dirty = true;
                        }
                    } else if (opcode == SHENGYI_OPCODE_CONFIG_C2) {
                        if (shengyi_frame_validate(frame, frame_len, SHENGYI_OPCODE_CONFIG_C2, 0u, NULL)) {
                            uint8_t resp[SHENGYI_MAX_FRAME_SIZE];
                            size_t rlen = shengyi_build_frame_0xC3(resp, sizeof(resp));
                            if (rlen)
                                motor_isr_queue_frame(resp, (uint8_t)rlen);
                        }
                    }
                }
            }

            if (opcode == SHENGYI_OPCODE_STATUS) {
                /* Update motor state from g_inputs (populated by main.c motor handler) */
                g_motor.rpm = g_inputs.cadence_rpm;
                g_motor.speed_dmph = g_inputs.speed_dmph;
                g_motor.torque_raw = g_inputs.torque_raw;
                /* g_motor.soc_pct and g_motor.err are set elsewhere */
                g_motor.last_ms = evt->timestamp;

                /* Update cache */
                g_motor_cmd.status.rpm = g_inputs.cadence_rpm;
                g_motor_cmd.status.speed_dmph = g_inputs.speed_dmph;
                g_motor_cmd.status.torque_raw = g_inputs.torque_raw;
                g_motor_cmd.status.power_w = g_inputs.power_w;
                g_motor_cmd.status.battery_dV = g_inputs.battery_dV;
                g_motor_cmd.status.battery_dA = g_inputs.battery_dA;
                g_motor_cmd.status.ctrl_temp_dC = g_inputs.ctrl_temp_dC;
                g_motor_cmd.status.soc_pct = g_motor.soc_pct;
                g_motor_cmd.status.err = g_motor.err;
                g_motor_cmd.status.assist_level = g_outputs.virtual_gear;
                g_motor_cmd.status.last_update_ms = evt->timestamp;
                g_motor_cmd.status.valid = true;
            }
            break;
        }

        case EVT_MOTOR_ERROR: {
            /* Protocol error */
            g_motor_cmd.stats.errors++;
            uint8_t error_code = (uint8_t)(evt->payload16 & 0xFF);

            /* Update motor error state */
            g_motor.err = error_code;
            break;
        }

        case EVT_MOTOR_READY: {
            /* Motor controller came online */
            g_motor_cmd.status.valid = true;
            /* Force command update */
            g_motor_cmd.cmd_dirty = true;
            motor_cmd_update_command();
            break;
        }

        case EVT_MOTOR_TIMEOUT: {
            /* Communication timeout */
            g_motor_cmd.stats.timeouts++;

            /* Mark motor state as stale */
            if (evt->timestamp - g_motor.last_ms > 500) {
                /* Motor offline */
                g_motor_cmd.status.valid = false;
            }
            break;
        }

        default:
            break;
    }
}

/*
 * Set assist level
 */
void motor_cmd_set_assist(uint8_t level)
{
    motor_cmd_set_active_gear(level);
}

/*
 * Set OEM assist gear count (1, 3, 5, 6, 9)
 */
uint8_t motor_cmd_set_oem_gear_count(uint8_t count)
{
    uint8_t applied = shengyi_assist_oem_max(count);
    if (applied == 0u)
        applied = 1u;

    if (g_vgears.count != applied) {
        g_vgears.count = applied;
        vgear_generate_scales(&g_vgears);
        if (g_active_vgear == 0u || g_active_vgear > g_vgears.count)
            g_active_vgear = g_vgears.count;
        g_motor_cmd.cmd_dirty = true;
        motor_cmd_update_command();
    }
    return applied;
}

/*
 * Set active gear (assist level index)
 */
void motor_cmd_set_active_gear(uint8_t idx)
{
    if (idx == 0u)
        idx = 1u;
    if (idx > g_vgears.count)
        idx = g_vgears.count;

    if (g_active_vgear != idx) {
        g_active_vgear = idx;
        g_motor_cmd.assist_level = idx;
        g_motor_cmd.cmd_dirty = true;
        motor_cmd_update_command();
    }
}

/*
 * Set headlight state
 */
void motor_cmd_set_light(bool on)
{
    if (g_motor_cmd.light_on != on) {
        g_motor_cmd.light_on = on;
        g_motor_cmd.cmd_dirty = true;
        motor_cmd_update_command();
    }
}

/*
 * Set walk assist state
 */
void motor_cmd_set_walk(bool active)
{
    if (g_motor_cmd.walk_active != active) {
        g_motor_cmd.walk_active = active;
        g_motor_cmd.cmd_dirty = true;
        motor_cmd_update_command();
    }
}

/*
 * Set speed limit flag
 */
void motor_cmd_set_speed_over(bool over)
{
    if (g_motor_cmd.speed_over != over) {
        g_motor_cmd.speed_over = over;
        g_motor_cmd.cmd_dirty = true;
        motor_cmd_update_command();
    }
}

/*
 * Get last received motor status
 */
void motor_cmd_get_status(motor_status_cache_t *status)
{
    if (status) {
        *status = g_motor_cmd.status;
    }
}

/*
 * Check if motor communication is healthy
 */
bool motor_cmd_is_alive(uint32_t now_ms)
{
    if (!g_motor_cmd.status.valid)
        return false;

    uint32_t age = now_ms - g_motor_cmd.status.last_update_ms;
    return age < 500;  /* 500ms timeout */
}

/*
 * Get command subsystem statistics
 */
void motor_cmd_get_stats(motor_cmd_stats_t *stats)
{
    if (stats) {
        *stats = g_motor_cmd.stats;
    }
}

/*
 * Update command if dirty
 */
static void motor_cmd_update_command(void)
{
    if (!g_motor_cmd.cmd_dirty)
        return;
    if (!shengyi_handshake_ok())
        return;

    /* Get mapped assist level from Shengyi DWG22 module */
    uint8_t mapped_level = motor_cmd_get_current_assist_mapped();
    bool battery_low = motor_cmd_battery_low_flag();

    /* Queue command to ISR */
    motor_isr_queue_cmd(
        mapped_level,
        g_motor_cmd.light_on,
        g_motor_cmd.walk_active,
        battery_low,
        g_motor_cmd.speed_over
    );

    g_motor_cmd.cmd_dirty = false;
}

/*
 * Get current assist level mapped to OEM protocol
 */
static uint8_t motor_cmd_get_current_assist_mapped(void)
{
    return shengyi_assist_level_mapped();
}

static bool motor_cmd_battery_low_flag(void)
{
    if (!(g_input_caps & INPUT_CAP_BATT_V))
        return false;
    return (shengyi_batt_soc_pct_from_dV(g_inputs.battery_dV) == 0u);
}

static void motor_cmd_handle_status_0x52(const uint8_t *frame, uint8_t len, uint32_t now_ms)
{
    if (!frame || len < 13)
        return;
    if (frame[0] != SHENGYI_FRAME_START || frame[2] != SHENGYI_OPCODE_STATUS)
        return;
    uint8_t payload_len = frame[3];
    if (payload_len < 5 || (uint8_t)(payload_len + 8u) > len)
        return;
    const uint8_t *p = &frame[4];

    uint8_t b0 = p[0];
    uint8_t b1 = p[1];
    uint16_t speed_raw = (uint16_t)((uint16_t)p[2] << 8) | p[3];
    uint8_t err = p[4];

    /* Battery voltage: low 6 bits are in volts */
    uint16_t batt_dV = (uint16_t)((b0 & 0x3Fu) * 10u);
    g_inputs.battery_dV = (int16_t)batt_dV;
    g_input_caps |= INPUT_CAP_BATT_V;

    /* Battery current: raw / 3 * 1000 mA */
    g_inputs.battery_dA = motor_cmd_current_dA_from_raw(b1);
    g_input_caps |= INPUT_CAP_BATT_I;

    /* Speed */
    uint16_t wheel_mm = g_config_active.wheel_mm ? g_config_active.wheel_mm : 2100u;
    g_inputs.speed_dmph = motor_cmd_speed_dmph_from_raw(speed_raw, wheel_mm);
    g_inputs.last_ms = now_ms;
    speed_rb_push(g_inputs.speed_dmph);
    shengyi_speed_update_target(g_inputs.speed_dmph);

    g_motor.speed_dmph = g_inputs.speed_dmph;
    g_motor.soc_pct = shengyi_batt_soc_pct_from_dV(g_inputs.battery_dV);
    g_motor.err = err ? err : (uint8_t)((b0 & 0x80u) ? 1u : 0u);
    g_motor.last_ms = now_ms;
}

static uint16_t motor_cmd_speed_dmph_from_raw(uint16_t speed_raw, uint16_t wheel_mm)
{
    if (speed_raw == 0 || wheel_mm == 0)
        return 0;
    uint32_t kph_x10 = ((uint32_t)wheel_mm * 36u + (speed_raw / 2u)) / speed_raw;
    uint32_t dmph = (kph_x10 * 62137u + 50000u) / 100000u;
    if (dmph > 9999u)
        dmph = 9999u;
    return (uint16_t)dmph;
}

static int16_t motor_cmd_current_dA_from_raw(uint8_t current_raw)
{
    uint32_t ma = ((uint32_t)current_raw * 1000u + 1u) / 3u;
    uint32_t dA = (ma + 50u) / 100u;
    if (dA > 32767u)
        dA = 32767u;
    return (int16_t)dA;
}
