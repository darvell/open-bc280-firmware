/*
 * Motor Command Processing Implementation
 *
 * Main loop handler for motor events from ISR queue.
 * Parses Shengyi DWG22 protocol responses and updates application state.
 */

#include "motor_cmd.h"
#include "motor_isr.h"
#include "motor_stx02.h"
#include "motor_link.h"
#include "shengyi.h"
#include "app_data.h"
#include "../control/control.h"
#include "../power/power.h"
#include "battery_soc.h"
#include "battery_monitor.h"
#include "../telemetry/telemetry.h"
#include "../config/config.h"
#include "../util/bool_to_u8.h"

#include <string.h>

#ifndef HOST_TEST
#include "../../platform/time.h"
#else
/* Host test stubs */
extern volatile uint32_t g_ms;
#endif

/* Keep a deterministic fallback if config has not been loaded. */
#define MOTOR_CMD_DEFAULT_WHEEL_MM SHENGYI_DEFAULT_WHEEL_MM
#define MOTOR_CMD_HEALTH_TIMEOUT_MS 500u
#define MOTOR_CMD_STX02_CMD1_OPCODE 1u
#define MOTOR_CMD_AUTH_XOR_OPCODE 0x46u
#define MOTOR_CMD_STALE_STATUS_AGE_MS 200u

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
    bool comm_fault_active;         /* Latched when link times out */
} g_motor_cmd;

/*
 * Forward declarations
 */
static void motor_cmd_update_command(void);
static uint8_t motor_cmd_get_current_assist_mapped(void);
static bool motor_cmd_battery_low_flag(void);
static bool motor_cmd_should_refresh_status_cache(uint8_t opcode, uint32_t now_ms);
static uint16_t motor_cmd_status_speed_dmph(uint8_t opcode, motor_proto_t proto);
static uint16_t motor_cmd_effective_wheel_mm(void);
static void motor_cmd_sync_motor_from_inputs(uint32_t now_ms);
static void motor_cmd_sync_status_cache(uint16_t speed_dmph, uint32_t now_ms);
static bool motor_cmd_handle_status_0x52(const uint8_t *frame, uint8_t len, uint32_t now_ms);
static void motor_cmd_record_parse_error(void);
static uint16_t motor_cmd_speed_dmph_from_raw(uint16_t speed_raw, uint16_t wheel_mm);
static int16_t motor_cmd_current_dA_from_raw(uint8_t current_raw);
static uint16_t motor_cmd_speed_dmph_from_period_ms(uint16_t period_ms, uint16_t wheel_mm);

static bool motor_cmd_handle_stx02_status_cmd1(const uint8_t *frame, uint8_t len, uint32_t now_ms);
static bool motor_cmd_handle_auth_status_f0x46(const uint8_t *frame, uint8_t len, uint32_t now_ms);
static bool motor_cmd_handle_v2_short(const uint8_t *frame, uint8_t len, uint32_t now_ms);

/*
 * Initialize motor command processor
 */
void motor_cmd_init(void)
{
    memset(&g_motor_cmd, 0, sizeof(g_motor_cmd));
    g_motor_cmd.status.valid = false;
    g_motor_cmd.cmd_dirty = true;  /* Force initial command send */
    g_motor_cmd.comm_fault_active = false;
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

            /* Payload: (proto<<8) | opcode (for Shengyi, opcode==frame[2]). */
            uint8_t opcode = (uint8_t)(evt->payload16 & 0xFFu);
            motor_proto_t proto = (motor_proto_t)(evt->payload16 >> 8);

            bool frame_handled = false;
            bool frame_updates_inputs = false;

            uint8_t frame[SHENGYI_MAX_FRAME_SIZE];
            uint8_t frame_len = 0;
            uint8_t frame_op = 0;
            uint8_t frame_seq = 0;
            motor_proto_t frame_proto = MOTOR_PROTO_SHENGYI_3A1A;
            uint16_t frame_aux16 = 0u;
            if (motor_isr_copy_last_frame(frame, sizeof(frame), &frame_len, &frame_op, &frame_seq,
                                          &frame_proto, &frame_aux16))
            {
                if (proto != frame_proto)
                {
                    /* Stale event vs last frame snapshot; ignore. */
                    break;
                }

                if (proto == MOTOR_PROTO_SHENGYI_3A1A)
                {
                    /* Shengyi uses the opcode embedded in the frame. */
                    if (frame_op != opcode)
                        break;

                    if (opcode == SHENGYI_OPCODE_STATUS)
                    {
                        frame_handled = motor_cmd_handle_status_0x52(frame, frame_len, evt->timestamp);
                        frame_updates_inputs = frame_handled;
                        if (!frame_handled)
                            motor_cmd_record_parse_error();
                    }
                    else if (opcode == SHENGYI_OPCODE_CONFIG_53)
                    {
                        if (shengyi_frame_validate(frame, frame_len, SHENGYI_OPCODE_CONFIG_53, 7u, NULL))
                        {
                            shengyi_notify_rx_opcode(opcode);
                            g_motor_cmd.cmd_dirty = true;
                            motor_cmd_update_command();
                            frame_handled = true;
                        }
                        else
                        {
                            motor_cmd_record_parse_error();
                        }
                    }
                    else if (opcode == SHENGYI_OPCODE_STATUS_C0)
                    {
                        const uint8_t *payload = NULL;
                        if (shengyi_frame_validate(frame, frame_len, SHENGYI_OPCODE_STATUS_C0, 52u, &payload))
                        {
                            uint8_t status = bool_to_u8(shengyi_apply_config_c0(payload, frame[3]));
                            uint8_t resp[16];
                            size_t rlen = shengyi_build_frame_0xC1(status, resp, sizeof(resp));
                            if (rlen)
                                motor_isr_queue_frame(resp, (uint8_t)rlen);
                            g_motor_cmd.cmd_dirty = true;
                            frame_handled = true;
                        }
                        else
                        {
                            motor_cmd_record_parse_error();
                        }
                    }
                    else if (opcode == SHENGYI_OPCODE_CONFIG_C2)
                    {
                        if (shengyi_frame_validate(frame, frame_len, SHENGYI_OPCODE_CONFIG_C2, 0u, NULL))
                        {
                            uint8_t resp[SHENGYI_MAX_FRAME_SIZE];
                            size_t rlen = shengyi_build_frame_0xC3(resp, sizeof(resp));
                            if (rlen)
                                motor_isr_queue_frame(resp, (uint8_t)rlen);
                            frame_handled = true;
                        }
                        else
                        {
                            motor_cmd_record_parse_error();
                        }
                    }
                    else if (opcode == SHENGYI_OPCODE_PROTO_SWITCH)
                    {
                        /* OEM v2.3.0: motor sends 0xAB to request protocol switch.
                         * payload[0] != 0 triggers switch, payload[1] = protocol index. */
                        const uint8_t *ab_payload = NULL;
                        if (shengyi_frame_validate(frame, frame_len, SHENGYI_OPCODE_PROTO_SWITCH, 2u, &ab_payload))
                        {
                            if (ab_payload[0] != 0u)
                                motor_link_switch_protocol(ab_payload[1]);
                            frame_handled = true;
                        }
                        else
                        {
                            motor_cmd_record_parse_error();
                        }
                    }
                }
                else if (proto == MOTOR_PROTO_STX02_XOR)
                {
                    if (opcode == MOTOR_CMD_STX02_CMD1_OPCODE)
                    {
                        frame_handled = motor_cmd_handle_stx02_status_cmd1(frame, frame_len, evt->timestamp);
                        frame_updates_inputs = frame_handled;
                        if (!frame_handled)
                            motor_cmd_record_parse_error();
                    }
                }
                else if (proto == MOTOR_PROTO_AUTH_XOR_CR)
                {
                    if (opcode == MOTOR_CMD_AUTH_XOR_OPCODE)
                    {
                        frame_handled = motor_cmd_handle_auth_status_f0x46(frame, frame_len, evt->timestamp);
                        frame_updates_inputs = frame_handled;
                        if (!frame_handled)
                            motor_cmd_record_parse_error();
                    }
                }
                else if (proto == MOTOR_PROTO_V2_FIXED)
                {
                    frame_handled = motor_cmd_handle_v2_short(frame, frame_len, evt->timestamp);
                    frame_updates_inputs = frame_handled;
                    if (!frame_handled)
                        motor_cmd_record_parse_error();
                }
            }

            if (frame_handled && proto == MOTOR_PROTO_SHENGYI_3A1A && opcode == SHENGYI_OPCODE_STATUS)
                motor_cmd_sync_motor_from_inputs(evt->timestamp);

            if (frame_updates_inputs && motor_cmd_should_refresh_status_cache(opcode, evt->timestamp))
            {
                motor_cmd_sync_status_cache(motor_cmd_status_speed_dmph(opcode, proto), evt->timestamp);
                g_motor_cmd.comm_fault_active = false;
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
            if (evt->timestamp - g_motor.last_ms > MOTOR_CMD_HEALTH_TIMEOUT_MS) {
                /* Motor offline */
                g_motor_cmd.status.valid = false;
                g_motor_cmd.comm_fault_active = true;
            }
            break;
        }

        default:
            break;
    }
}

static uint16_t motor_cmd_speed_dmph_from_period_ms(uint16_t period_ms, uint16_t wheel_mm)
{
    if (period_ms == 0u || wheel_mm == 0u)
        return 0u;

    /* OEM-style: speed_kmh = (3.6 * wheel_mm) / period_ms where period_ms is ms/rev. */
    uint32_t kmh_x10 = (36u * (uint32_t)wheel_mm) / (uint32_t)period_ms;
    /* deci-mph = kmh_x10 * 0.621371... */
    uint32_t dmph = (kmh_x10 * 621u + 500u) / 1000u;
    /* OEM caps at 99.9 kph (~62.1 mph). */
    if (dmph > 621u)
        dmph = 621u;
    return (uint16_t)dmph;
}

static bool motor_cmd_handle_stx02_status_cmd1(const uint8_t *frame, uint8_t len, uint32_t now_ms)
{
    /* OEM v2.5.1 (mode=1): 0x02 len cmd payload... xor. cmd=1 payload is 10 bytes. */
    motor_stx02_cmd1_t s;
    if (!motor_stx02_decode_cmd1(frame, len, &s))
    {
        return false;
    }

    uint16_t wheel_mm = motor_cmd_effective_wheel_mm();
    uint16_t speed_dmph = 0u;

    /*
     * OEM: if period_ms >= 3000, treat speed as 0.
     * Evidence: APP_process_motor_response_packet @ 0x08021CA8 compares to 0x0BB8.
     */
    if (s.period_ms > 0u && s.period_ms < 3000u)
        speed_dmph = motor_cmd_speed_dmph_from_period_ms(s.period_ms, wheel_mm);

    g_motor.err = s.err_code;
    g_motor.speed_dmph = speed_dmph;
    if (s.soc_valid)
        g_motor.soc_pct = s.soc_pct;
    g_motor.last_ms = now_ms;

    g_inputs.speed_dmph = speed_dmph;
    g_inputs.battery_dA = s.current_dA;
    g_input_caps |= INPUT_CAP_BATT_I;
    /*
     * OEM stores bit2/bit7 from STX02 cmd1 as internal status flags; for JH0 we
     * do not have evidence that bit2 is a direct brake-cut signal in this path.
     */
    g_inputs.brake = 0u;
    g_inputs.last_ms = now_ms;

    speed_rb_push(speed_dmph);
    return true;
}

static bool motor_cmd_handle_auth_status_f0x46(const uint8_t *frame, uint8_t len, uint32_t now_ms)
{
    /*
     * OEM v2.5.1 (mode=3): auth-like frames start with 0x46 ('F').
     * The OEM processes 6 bytes after the SOF as a "battery status packet".
     *
     * We mirror the speed math for compatibility, but keep parsing conservative.
     */
    if (!frame || len < (uint8_t)(1u + 6u + 2u))
        return false;
    if (frame[0] != 0x46u)
        return false;

    const uint8_t *p = &frame[1];

    /* OEM v2.5.1 `APP_process_battery_status_packet` @ 0x8023774 consumes 6 bytes:
     * p[0] -> if a selector is 1, sets soc_pct-like field = 20*p[0]
     * p[1] -> (p[1]/3.0)*1000 (looks like current-like scaling)
     * p[2..3] -> wheel period (ms/rev) */
    uint8_t soc_pct = (uint8_t)(p[0] * 20u);
    if (soc_pct > 100u) soc_pct = 100u;

    /* Convert OEM's (byte/3.0)*1000 mA to deci-amps.
     * dA = mA/100 = (byte*1000/3)/100 = byte*10/3. */
    int16_t batt_dA = (int16_t)(((int32_t)p[1] * 10 + 1) / 3);

    uint16_t period_ms = (uint16_t)((uint16_t)p[2] << 8) | p[3];
    uint16_t wheel_mm = motor_cmd_effective_wheel_mm();
    uint16_t speed_dmph = 0u;
    if (period_ms > 0u && period_ms < 3000u)
        speed_dmph = motor_cmd_speed_dmph_from_period_ms(period_ms, wheel_mm);

    g_motor.speed_dmph = speed_dmph;
    g_motor.soc_pct = soc_pct;
    g_motor.last_ms = now_ms;
    g_inputs.speed_dmph = speed_dmph;
    g_inputs.battery_dA = batt_dA;
    g_inputs.brake = 0u;
    g_input_caps |= INPUT_CAP_BATT_I;
    g_inputs.last_ms = now_ms;
    return true;
}

static bool motor_cmd_handle_v2_short(const uint8_t *frame, uint8_t len, uint32_t now_ms)
{
    if (!frame || len != 5u)
        return false;

    /* Best-effort: treat bytes 2/3 as a period ms (big-endian) if plausible. */
    uint16_t period_ms = (uint16_t)((uint16_t)frame[2] << 8) | frame[3];
    if (period_ms < 50u || period_ms > 5000u)
        return false;

    uint16_t wheel_mm = motor_cmd_effective_wheel_mm();
    uint16_t speed_dmph = motor_cmd_speed_dmph_from_period_ms(period_ms, wheel_mm);
    g_motor.speed_dmph = speed_dmph;
    g_motor.last_ms = now_ms;
    g_inputs.speed_dmph = speed_dmph;
    g_inputs.brake = 0u;
    g_inputs.last_ms = now_ms;
    return true;
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
    return age < MOTOR_CMD_HEALTH_TIMEOUT_MS;
}

bool motor_cmd_link_fault_active(void)
{
    return g_motor_cmd.comm_fault_active;
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
    if (motor_isr_queue_cmd(
        mapped_level,
        g_motor_cmd.light_on,
        g_motor_cmd.walk_active,
        battery_low,
        g_motor_cmd.speed_over))
    {
        g_motor_cmd.cmd_dirty = false;
    }
}

/*
 * Get current assist level mapped to OEM protocol
 */
static uint8_t motor_cmd_get_current_assist_mapped(void)
{
    return shengyi_assist_level_mapped();
}

static bool motor_cmd_should_refresh_status_cache(uint8_t opcode, uint32_t now_ms)
{
    if (opcode == SHENGYI_OPCODE_STATUS)
        return true;
    return (g_motor.last_ms == now_ms || g_inputs.last_ms == now_ms);
}

static uint16_t motor_cmd_effective_wheel_mm(void)
{
    return g_config_active.wheel_mm ? g_config_active.wheel_mm : MOTOR_CMD_DEFAULT_WHEEL_MM;
}

static void motor_cmd_record_parse_error(void)
{
    g_motor_cmd.stats.parse_errors++;
}

static uint16_t motor_cmd_status_speed_dmph(uint8_t opcode, motor_proto_t proto)
{
    if (proto == MOTOR_PROTO_SHENGYI_3A1A && opcode == SHENGYI_OPCODE_STATUS)
        return g_inputs.speed_dmph;
    return (g_motor.speed_dmph != 0u) ? g_motor.speed_dmph : g_inputs.speed_dmph;
}

static void motor_cmd_sync_motor_from_inputs(uint32_t now_ms)
{
    g_motor.rpm = g_inputs.cadence_rpm;
    g_motor.speed_dmph = g_inputs.speed_dmph;
    g_motor.torque_raw = g_inputs.torque_raw;
    g_motor.last_ms = now_ms;
}

static void motor_cmd_sync_status_cache(uint16_t speed_dmph, uint32_t now_ms)
{
    g_motor_cmd.status.rpm = g_inputs.cadence_rpm;
    g_motor_cmd.status.speed_dmph = speed_dmph;
    g_motor_cmd.status.torque_raw = g_inputs.torque_raw;
    g_motor_cmd.status.power_w = g_inputs.power_w;
    g_motor_cmd.status.battery_dV = g_inputs.battery_dV;
    g_motor_cmd.status.battery_dA = g_inputs.battery_dA;
    g_motor_cmd.status.ctrl_temp_dC = g_inputs.ctrl_temp_dC;
    g_motor_cmd.status.soc_pct = g_motor.soc_pct;
    g_motor_cmd.status.err = g_motor.err;
    g_motor_cmd.status.assist_level = g_outputs.virtual_gear;
    g_motor_cmd.status.last_update_ms = now_ms;
    g_motor_cmd.status.valid = true;
}

static bool motor_cmd_battery_low_flag(void)
{
    if (!(g_input_caps & INPUT_CAP_BATT_V))
        return false;
    uint32_t batt_mv = (uint32_t)((g_inputs.battery_dV > 0) ? g_inputs.battery_dV : 0) * 100u;
    return (battery_soc_pct_from_mv(batt_mv, shengyi_nominal_v()) == 0u);
}

static bool motor_cmd_handle_status_0x52(const uint8_t *frame, uint8_t len, uint32_t now_ms)
{
    if (!frame || len < 13)
        return false;
    if (frame[0] != SHENGYI_FRAME_START || frame[2] != SHENGYI_OPCODE_STATUS)
        return false;
    uint8_t payload_len = frame[3];
    if (payload_len < 5 || (uint8_t)(payload_len + 8u) > len)
        return false;
    const uint8_t *p = &frame[4];

    uint8_t b0 = p[0];
    uint8_t b1 = p[1];
    uint16_t speed_raw = (uint16_t)((uint16_t)p[2] << 8) | p[3];
    uint8_t err_raw = p[4];
    uint8_t err = err_raw;
    if (err_raw != 0u && (err_raw < 33u || err_raw > 38u))
    {
        /* Preserve the presence of an unknown controller fault code. */
        err = 0xFFu;
    }

    /* Battery voltage: low 6 bits are in volts.
     * OEM v2.5.1 also measures battery via ADC (PA0/ADC1). Prefer ADC when
     * it is active to avoid clobbering higher-resolution readings. */
    uint16_t batt_dV_status = (uint16_t)((b0 & 0x3Fu) * 10u);
    bool adc_ok = battery_monitor_has_sample();
    uint32_t adc_age = adc_ok ? (uint32_t)(now_ms - battery_monitor_last_update_ms()) : 0xFFFFFFFFu;
    if (!adc_ok || adc_age > MOTOR_CMD_STALE_STATUS_AGE_MS)
    {
        g_inputs.battery_dV = (int16_t)batt_dV_status;
        g_input_caps |= INPUT_CAP_BATT_V;
    }

    /* Brake flag (OEM-aligned): byte0 bit6. */
    g_inputs.brake = bool_to_u8(b0 & 0x40u);

    /* Battery current: raw / 3 * 1000 mA */
    g_inputs.battery_dA = motor_cmd_current_dA_from_raw(b1);
    g_input_caps |= INPUT_CAP_BATT_I;

    /* Speed */
    uint16_t wheel_mm = motor_cmd_effective_wheel_mm();
    g_inputs.speed_dmph = motor_cmd_speed_dmph_from_raw(speed_raw, wheel_mm);
    g_inputs.last_ms = now_ms;
    speed_rb_push(g_inputs.speed_dmph);
    shengyi_speed_update_target(g_inputs.speed_dmph);

    g_motor.speed_dmph = g_inputs.speed_dmph;
    uint32_t batt_mv = (uint32_t)((g_inputs.battery_dV > 0) ? g_inputs.battery_dV : 0) * 100u; /* 0.1V -> mV */
    g_motor.soc_pct = battery_soc_pct_from_mv(batt_mv, shengyi_nominal_v());
    g_motor.err = err;
    g_motor.last_ms = now_ms;
    return true;
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
