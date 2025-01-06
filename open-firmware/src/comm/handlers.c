#include "comm.h"

#include <stddef.h>
#include <stdint.h>

#include "app_data.h"
#include "app_state.h"
#include "ui.h"
#include "ui_state.h"
#include "ble_hacker.h"
#include "src/config/config.h"
#include "src/control/control.h"
#include "src/power/power.h"
#include "src/input/input.h"
#include "src/bus/bus.h"
#include "src/profiles/profiles.h"
#include "src/telemetry/trip.h"
#include "src/telemetry/telemetry.h"
#include "src/system_control.h"
#include "src/motor/shengyi.h"
#include "platform/mmio.h"
#include "platform/time.h"
#include "drivers/spi_flash.h"
#include "storage/layout.h"
#include "storage/logs.h"
#include "storage/ab_update.h"
#include "storage/crash_dump.h"
#include "util/byteorder.h"
#include "src/core/math_util.h"

extern uint32_t g_inputs_debug_last_ms;
extern uint8_t g_watchdog_feed_enabled;
extern uint32_t g_last_profile_switch_ms;
extern uint16_t g_reset_flags;
extern uint32_t g_reset_csr;
extern uint16_t g_gear_limit_power_w;
extern uint16_t g_gear_scale_q15;
extern uint16_t g_cadence_bias_q15;
extern uint8_t g_last_brake_state;

void process_buttons(uint8_t raw_buttons);
void recompute_outputs(void);

enum {
    CONFIG_CHANGE_MAX_SPEED_DMPH = 10u /* 1.0 mph */
};

static uint16_t config_change_speed_dmph(void)
{
    uint16_t spd = g_inputs.speed_dmph;
    if (g_motor.speed_dmph > spd)
        spd = g_motor.speed_dmph;
    return spd;
}

static int config_change_allowed(void)
{
    return config_change_speed_dmph() <= CONFIG_CHANGE_MAX_SPEED_DMPH;
}

static int config_change_guard(uint8_t cmd)
{
    if (config_change_allowed())
        return 1;
    send_status(cmd, 0xFC); /* blocked while moving */
    return 0;
}


static uint8_t g_last_log[LOG_FRAME_MAX];
static uint8_t g_last_log_len;

static void log_set_bytes(const uint8_t *p, uint8_t len)
{
    if (!p)
    {
        g_last_log_len = 0;
        return;
    }
    if (len > LOG_FRAME_MAX)
        len = LOG_FRAME_MAX;
    for (uint8_t i = 0; i < len; i++)
        g_last_log[i] = p[i];
    g_last_log_len = len;
}

static void handle_ping(uint8_t cmd)
{
    send_status(cmd, 0);
}

static void handle_log_frame(uint8_t cmd)
{
    send_frame_port(g_last_rx_port, cmd, g_last_log, g_last_log_len);
}

static void handle_read32(const uint8_t *p, uint8_t len, uint8_t cmd)
{
    if (len < 4)
        return;
    uint32_t addr = (p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3];
    uint32_t v = mmio_read32(addr);
    uint8_t out[4] = { (uint8_t)(v >> 24), (uint8_t)(v >> 16), (uint8_t)(v >> 8), (uint8_t)v };
    send_frame_port(g_last_rx_port, cmd | 0x80, out, 4);
}

static void handle_write32(const uint8_t *p, uint8_t len, uint8_t cmd)
{
    if (len < 8)
        return;
    uint32_t addr = (p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3];
    uint32_t v = (p[4] << 24) | (p[5] << 16) | (p[6] << 8) | p[7];
    mmio_write32(addr, v);
    send_status(cmd, 0);
}

static void handle_read_mem(const uint8_t *p, uint8_t len, uint8_t cmd)
{
    if (len < 5)
        return;
    uint32_t addr = (p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3];
    uint8_t n = p[4];
    if (n > COMM_MAX_PAYLOAD || n == 0)
        return;
    uint8_t out[COMM_MAX_PAYLOAD];
    for (uint8_t i = 0; i < n; i++)
        out[i] = *(volatile uint8_t *)(addr + i);
    send_frame_port(g_last_rx_port, cmd | 0x80, out, n);
}

static void handle_write_mem(const uint8_t *p, uint8_t len, uint8_t cmd)
{
    if (len < 5)
        return;
    uint32_t addr = (p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3];
    uint8_t n = p[4];
    if (n == 0 || n > (len - 5))
        return;
    for (uint8_t i = 0; i < n; i++)
        *(volatile uint8_t *)(addr + i) = p[5 + i];
    send_status(cmd, 0);
}

typedef void (*entry_fn_t)(void);

static void handle_exec(const uint8_t *p, uint8_t len, uint8_t cmd)
{
    if (len < 4)
        return;
    uint32_t addr = (p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3];
    entry_fn_t fn = (entry_fn_t)(uintptr_t)addr;
    send_status(cmd, 0); /* respond before jumping */
    fn();
}

static void handle_upload_exec(const uint8_t *p, uint8_t len, uint8_t cmd)
{
    if (len < 5)
        return;
    uint32_t addr = (p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3];
    uint8_t n = p[4];
    if (n == 0 || n > (len - 5))
        return;
    for (uint8_t i = 0; i < n; i++)
        *(volatile uint8_t *)(addr + i) = p[5 + i];
    send_status(cmd, 0);
    ((entry_fn_t)(uintptr_t)addr)();
}

static void handle_read_flash(const uint8_t *p, uint8_t len, uint8_t cmd)
{
    if (len < 5)
        return;
    uint32_t addr = (p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3];
    uint8_t n = p[4];
    if (n == 0 || n > COMM_MAX_PAYLOAD)
        return;

    /* External flash is not memory-mapped on hardware; support reads via SPI.
     * - Renode stubs map SPI flash at 0x0030_0000.
     * - The AT32 SPIM window maps at 0x0840_0000 (16MB), if enabled.
     */
    if (addr >= SPI_FLASH_STORAGE_BASE && addr < 0x08000000u)
    {
        uint8_t buf[COMM_MAX_PAYLOAD];
        spi_flash_read(addr, buf, n);
        send_frame_port(g_last_rx_port, cmd | 0x80, buf, n);
        return;
    }
    if (addr >= 0x08400000u && addr < 0x09400000u)
    {
        uint8_t buf[COMM_MAX_PAYLOAD];
        spi_flash_read(addr - 0x08400000u, buf, n);
        send_frame_port(g_last_rx_port, cmd | 0x80, buf, n);
        return;
    }

    const uint8_t *ptr = (const uint8_t *)(uintptr_t)addr;
    send_frame_port(g_last_rx_port, cmd | 0x80, ptr, n);
}

static void handle_set_bootloader_flag(uint8_t cmd)
{
    /* Mirror stock behavior: set g_bootloader_mode_flag (SPI flash) so OEM bootloader stays in update mode. */
    spi_flash_set_bootloader_mode_flag();
    send_status(cmd, 0);
}

static void handle_state_dump(uint8_t cmd)
{
    uint8_t out[16];
    out[0] = (g_ms >> 24) & 0xFF;
    out[1] = (g_ms >> 16) & 0xFF;
    out[2] = (g_ms >> 8) & 0xFF;
    out[3] = g_ms & 0xFF;
    out[4] = g_motor.rpm >> 8;
    out[5] = g_motor.rpm & 0xFF;
    out[6] = g_motor.torque_raw >> 8;
    out[7] = g_motor.torque_raw & 0xFF;
    out[8] = g_motor.speed_dmph >> 8;
    out[9] = g_motor.speed_dmph & 0xFF;
    out[10] = g_motor.soc_pct;
    out[11] = g_motor.err;
    out[12] = (g_motor.last_ms >> 8) & 0xFF;
    out[13] = g_motor.last_ms & 0xFF;
    out[14] = 0;
    out[15] = 0;
    send_frame_port(g_last_rx_port, cmd | 0x80, out, 16);
}

#define DEBUG_STATE_VERSION 19
#define DEBUG_STATE_V2_SIZE 122
#define DEBUG_STATE_MIN_SIZE 28


static void handle_debug_state_v2(uint8_t cmd)
{
    uint8_t out[DEBUG_STATE_V2_SIZE];
    for (size_t i = 0; i < DEBUG_STATE_V2_SIZE; ++i)
        out[i] = 0;
    out[0] = DEBUG_STATE_VERSION;
    out[1] = DEBUG_STATE_V2_SIZE;
    store_be32(&out[2], g_ms);
    store_be32(&out[6], g_inputs.last_ms);
    store_be16(&out[10], g_inputs.speed_dmph);
    store_be16(&out[12], g_inputs.cadence_rpm);
    store_be16(&out[14], g_inputs.torque_raw);
    out[16] = g_inputs.throttle_pct;
    out[17] = g_inputs.brake;
    out[18] = g_inputs.buttons;
    out[19] = g_outputs.assist_mode;
    out[20] = g_outputs.profile_id;
    out[21] = g_outputs.virtual_gear;
    store_be16(&out[22], g_outputs.cmd_power_w);
    store_be16(&out[24], g_outputs.cmd_current_dA);
    out[26] = g_outputs.cruise_state;
    out[27] = g_adapt.eco_clamp_active ? 1u : 0u;
    /* Profile caps for Renode assertions */
    const assist_profile_t *p = &g_profiles[g_outputs.profile_id];
    store_be16(&out[28], p->cap_power_w);
    store_be16(&out[30], g_effective_cap_current_dA);
    store_be16(&out[32], g_effective_cap_speed_dmph);
    /* Curve-derived internal values (optional assertions) */
    store_be16(&out[34], g_curve_power_w);
    store_be16(&out[36], g_curve_cadence_q15);
    /* mirror speed cap for clarity */
    store_be16(&out[38], g_effective_cap_speed_dmph);
    /* virtual gear + cadence bias internals */
    store_be16(&out[40], g_gear_limit_power_w);
    store_be16(&out[42], g_gear_scale_q15);
    store_be16(&out[44], g_cadence_bias_q15);
    out[46] = (uint8_t)g_walk_state;
    store_be16(&out[47], g_walk_cmd_power_w);
    store_be16(&out[49], g_walk_cmd_current_dA);
    out[51] = g_config_active.mode;
    store_be16(&out[52], g_effective_cap_current_dA);
    store_be16(&out[54], g_effective_cap_speed_dmph);
    store_be16(&out[56], (uint16_t)g_adapt.speed_delta_dmph);
    store_be16(&out[58], g_power_policy.p_user_w);
    store_be16(&out[60], g_power_policy.p_lug_w);
    store_be16(&out[62], g_power_policy.p_thermal_w);
    store_be16(&out[64], g_power_policy.p_sag_w);
    store_be16(&out[66], g_power_policy.p_final_w);
    out[68] = g_power_policy.limit_reason;
    out[69] = g_adapt.trend_active ? 1u : 0u;
    store_be16(&out[70], g_power_policy.duty_q16);
    store_be16(&out[72], (uint16_t)g_power_policy.i_phase_est_dA);
    store_be16(&out[74], g_power_policy.thermal_state);
    store_be16(&out[76], (uint16_t)g_power_policy.sag_margin_dV);
    out[78] = g_soft_start.active ? 1u : 0u;
    out[79] = 0;
    store_be16(&out[80], g_soft_start.output_w);
    store_be16(&out[82], g_soft_start.target_w);
    store_be16(&out[84], g_reset_flags);
    store_be32(&out[86], g_reset_csr);
    store_be16(&out[90], g_range_wh_per_mile_d10);
    store_be16(&out[92], g_range_est_d10);
    out[94] = g_range_confidence;
    out[95] = (uint8_t)((g_range_count > 255u) ? 255u : g_range_count);
    out[96] = (uint8_t)g_drive.mode;
    store_be16(&out[97], g_drive.setpoint);
    store_be16(&out[99], g_drive.cmd_power_w);
    store_be16(&out[101], g_drive.cmd_current_dA);
    store_be16(&out[103], g_boost.budget_ms);
    out[105] = g_boost.active;
    store_be16(&out[106], g_config_active.boost_threshold_dA);
    store_be16(&out[108], g_config_active.boost_gain_q15);
    out[110] = g_hw_caps;
    out[111] = regen_capable() ? 1u : 0u;
    out[112] = g_regen.level;
    out[113] = g_regen.brake_level;
    store_be16(&out[114], g_regen.cmd_power_w);
    store_be16(&out[116], g_regen.cmd_current_dA);
    out[118] = (g_config_active.button_flags & BUTTON_FLAG_LOCK_ENABLE) ? 1u : 0u;
    out[119] = g_lock_active ? 1u : 0u;
    out[120] = g_lock_allowed_mask;
    out[121] = g_quick_action_last;
    send_frame_port(g_last_rx_port, cmd | 0x80, out, DEBUG_STATE_V2_SIZE);
}

static void handle_config_get(uint8_t cmd)
{
    uint8_t out[CONFIG_BLOB_SIZE];
    config_store_be(out, &g_config_active);
    send_frame_port(g_last_rx_port, cmd | 0x80, out, CONFIG_BLOB_SIZE);
}

static void handle_config_stage(const uint8_t *p, uint8_t len, uint8_t cmd)
{
    if (len < CONFIG_BLOB_SIZE)
        return;
    if (!config_change_guard(cmd))
        return;
    uint8_t status = config_stage_blob(p);
    send_status(cmd, status);
}

static void handle_config_commit(const uint8_t *p, uint8_t len, uint8_t cmd)
{
    if (!config_change_guard(cmd))
        return;
    uint8_t status = config_commit_staged(p, len);
    send_status(cmd, status);
}

static void handle_ab_status(uint8_t cmd)
{
    uint8_t out[12];
    out[0] = 1;
    out[1] = 12;
    out[2] = g_ab_active_slot;
    out[3] = g_ab_pending_slot;
    out[4] = g_ab_last_good_slot;
    out[5] = 0;
    if (g_ab_active_valid)
        out[5] |= 0x01u;
    if (g_ab_pending_valid)
        out[5] |= 0x02u;
    store_be32(&out[6], g_ab_active_build_id);
    out[10] = 0;
    out[11] = 0;
    send_frame_port(g_last_rx_port, cmd | 0x80, out, (uint8_t)sizeof(out));
}

static void handle_ab_set_pending(const uint8_t *p, uint8_t len, uint8_t cmd)
{
    if (len < 1 || !p)
        return;
    uint8_t slot = p[0];
    uint8_t status = ab_update_set_pending(slot);
    send_status(cmd, status);
}

static void handle_set_profile(const uint8_t *p, uint8_t len, uint8_t cmd)
{
    if (len < 1)
        return;
    uint8_t id = p[0];
    int persist = (len >= 2) ? (p[1] != 0) : 1; /* default persist */
    if (persist && !config_change_guard(cmd))
        return;
    uint8_t status = (uint8_t)set_active_profile(id, persist);
    send_status(cmd, status);
}

static void set_active_gear(uint8_t idx)
{
    if (idx == 0)
        idx = 1;
    if (idx > g_vgears.count)
        idx = g_vgears.count;
    g_active_vgear = idx;
}

static void handle_set_gears(const uint8_t *p, uint8_t len, uint8_t cmd)
{
    if (len < 6)
        return;
    if (!config_change_guard(cmd))
        return;
    vgear_table_t t = {0};
    t.count = p[0];
    t.shape = p[1];
    t.min_scale_q15 = load_be16(&p[2]);
    t.max_scale_q15 = load_be16(&p[4]);
    if (t.count == 0 || t.count > VGEAR_MAX)
    {
        send_status(cmd, 0xFE);
        return;
    }
    if (len >= (uint8_t)(6 + t.count * 2u))
    {
        /* Explicit per-gear scales provided. */
        const uint8_t *sp = &p[6];
        for (uint8_t i = 0; i < t.count; ++i)
            t.scales[i] = load_be16(&sp[i * 2u]);
    }
    else
    {
        vgear_generate_scales(&t);
    }
    if (!vgear_validate(&t))
    {
        send_status(cmd, 0xFD);
        return;
    }
    g_vgears = t;
    set_active_gear(g_active_vgear);
    send_status(cmd, 0);
}

static void handle_set_cadence_bias(const uint8_t *p, uint8_t len, uint8_t cmd)
{
    if (len < 7)
        return;
    if (!config_change_guard(cmd))
        return;
    cadence_bias_t cb = g_cadence_bias;
    cb.enabled = p[0] ? 1 : 0;
    cb.target_rpm = load_be16(&p[1]);
    cb.band_rpm = load_be16(&p[3]);
    cb.min_bias_q15 = load_be16(&p[5]);
    if (cb.band_rpm == 0)
    {
        send_status(cmd, 0xFE);
        return;
    }
    cb.min_bias_q15 = clamp_q15(cb.min_bias_q15, 0, 32768u);
    g_cadence_bias = cb;
    send_status(cmd, 0);
}

static void handle_set_drive_mode(const uint8_t *p, uint8_t len, uint8_t cmd)
{
    if (len < 1)
        return;
    if (!config_change_guard(cmd))
        return;
    drive_mode_t mode = (drive_mode_t)p[0];
    if (mode > DRIVE_MODE_SPORT)
    {
        send_status(cmd, 0xFE);
        return;
    }
    uint16_t setpoint = 0;
    if (len >= 3)
        setpoint = load_be16(&p[1]);
    if (mode == DRIVE_MODE_MANUAL_CURRENT)
    {
        if (setpoint > MANUAL_CURRENT_MAX_DA)
            setpoint = MANUAL_CURRENT_MAX_DA;
    }
    else if (mode == DRIVE_MODE_MANUAL_POWER)
    {
        if (setpoint > MANUAL_POWER_MAX_W)
            setpoint = MANUAL_POWER_MAX_W;
    }
    else
    {
        setpoint = 0;
    }
    g_drive.mode = mode;
    g_drive.setpoint = setpoint;
    g_drive.cmd_power_w = 0;
    g_drive.cmd_current_dA = 0;
    g_drive.last_ms = 0;
    if (mode != DRIVE_MODE_SPORT)
        g_boost.budget_ms = g_config_active.boost_budget_ms;
    send_status(cmd, 0);
}

static void handle_set_regen(const uint8_t *p, uint8_t len, uint8_t cmd)
{
    if (len < 2)
        return;
    if (!config_change_guard(cmd))
        return;
    if (!regen_capable())
    {
        regen_reset();
        send_status(cmd, 0xFD);
        return;
    }
    regen_set_levels(p[0], p[1]);
    send_status(cmd, 0);
}

static void handle_set_hw_caps(const uint8_t *p, uint8_t len, uint8_t cmd)
{
    if (len < 1)
        return;
    if (!config_change_guard(cmd))
        return;
    g_hw_caps = (uint8_t)(p[0] & (CAP_FLAG_WALK | CAP_FLAG_REGEN));
    if (!(g_hw_caps & CAP_FLAG_REGEN))
        regen_reset();
    send_status(cmd, 0);
}

static void handle_trip_get(uint8_t cmd)
{
    trip_snapshot_t cur;
    trip_get_current(&cur);

    trip_snapshot_t last = {0};
    int has_last = trip_get_last(&last);

    uint8_t out[3 + 24 + 24];
    out[0] = TRIP_VERSION;
    out[1] = (uint8_t)sizeof(out);
    out[2] = has_last ? 1u : 0u; /* flags */
    trip_snapshot_to_be(&out[3], &cur);
    trip_snapshot_to_be(&out[27], &last);
    send_frame_port(g_last_rx_port, cmd | 0x80, out, (uint8_t)sizeof(out));
}

static void handle_trip_reset(uint8_t cmd)
{
    trip_finalize_and_persist();
    send_status(cmd, 0);
}

static void log_summary_base(uint8_t *out, uint8_t version, uint8_t size,
                             uint16_t count, uint16_t capacity,
                             uint16_t head, uint16_t record_size)
{
    out[0] = version;
    out[1] = size;
    store_be16(&out[2], count);
    store_be16(&out[4], capacity);
    store_be16(&out[6], head);
    store_be16(&out[8], record_size);
}

static uint8_t log_read_frame(const uint8_t *p, uint8_t len, uint16_t record_size,
                              uint8_t (*copy_fn)(uint16_t, uint8_t, uint8_t *),
                              uint8_t *out, uint8_t max_records)
{
    if (len < 3)
        return 0;
    uint16_t offset = load_be16(&p[0]);
    uint8_t want = p[2];
    if (want == 0 || want > max_records)
        want = max_records;
    uint8_t got = copy_fn(offset, want, &out[1]);
    out[0] = got;
    return (uint8_t)(1u + got * record_size);
}

static void handle_event_log_summary(uint8_t cmd)
{
    uint8_t out[16];
    log_summary_base(out, EVENT_LOG_VERSION, (uint8_t)sizeof(out),
                     (uint16_t)g_event_meta.count, (uint16_t)g_event_meta.capacity,
                     (uint16_t)g_event_meta.head, EVENT_LOG_RECORD_SIZE);
    store_be16(&out[10], 0);
    store_be32(&out[12], g_event_meta.seq);
    send_frame_port(g_last_rx_port, cmd | 0x80, out, (uint8_t)sizeof(out));
}

static void handle_event_log_read(const uint8_t *p, uint8_t len, uint8_t cmd)
{
    uint8_t out[1 + 8 * EVENT_LOG_RECORD_SIZE];
    uint8_t out_len = log_read_frame(p, len, EVENT_LOG_RECORD_SIZE, event_log_copy, out, 8);
    if (!out_len)
        return;
    send_frame_port(g_last_rx_port, cmd | 0x80, out, out_len);
}

static void handle_event_log_mark(const uint8_t *p, uint8_t len, uint8_t cmd)
{
    uint8_t type = EVT_TEST_MARK;
    uint8_t flags = 0;
    if (len >= 1)
        type = p[0];
    if (len >= 2)
        flags = p[1];
    event_log_append(type, flags);
    send_status(cmd, 0);
}

static void handle_stream_log_summary(uint8_t cmd)
{
    uint8_t out[18];
    log_summary_base(out, STREAM_LOG_VERSION, (uint8_t)sizeof(out),
                     (uint16_t)g_stream_meta.count, (uint16_t)g_stream_meta.capacity,
                     (uint16_t)g_stream_meta.head, STREAM_LOG_RECORD_SIZE);
    store_be16(&out[10], g_stream_log_period_ms);
    out[12] = g_stream_log_enabled ? 1u : 0u;
    out[13] = 0;
    store_be32(&out[14], g_stream_meta.seq);
    send_frame_port(g_last_rx_port, cmd | 0x80, out, (uint8_t)sizeof(out));
}

static void handle_stream_log_read(const uint8_t *p, uint8_t len, uint8_t cmd)
{
    uint8_t out[1 + 8 * STREAM_LOG_RECORD_SIZE];
    uint8_t out_len = log_read_frame(p, len, STREAM_LOG_RECORD_SIZE, stream_log_copy, out, 8);
    if (!out_len)
        return;
    send_frame_port(g_last_rx_port, cmd | 0x80, out, out_len);
}

static void handle_stream_log_control(const uint8_t *p, uint8_t len, uint8_t cmd)
{
    if (len < 1)
        return;
    uint8_t enable = p[0];
    if (!enable)
    {
        g_stream_log_enabled = 0;
        send_status(cmd, 0);
        return;
    }
    uint16_t period = g_config_active.log_period_ms;
    if (len >= 3)
        period = load_be16(&p[1]);
    g_stream_log_period_ms = stream_log_period_sanitize(period);
    g_stream_log_enabled = 1;
    g_stream_log_last_ms = g_ms;
    g_stream_log_last_sample_ms = 0;
    send_status(cmd, 0);
}

static void handle_crash_dump_read(uint8_t cmd)
{
    uint8_t out[CRASH_DUMP_SIZE];
    (void)crash_dump_load(out);
    send_frame_port(g_last_rx_port, cmd | 0x80, out, (uint8_t)sizeof(out));
}

static void handle_crash_dump_clear(uint8_t cmd)
{
    crash_dump_clear_storage();
    send_status(cmd, 0);
}

static void handle_bus_capture_summary(uint8_t cmd)
{
    uint8_t out[14];
    bus_capture_state_t state;
    bus_capture_get_state(&state);
    out[0] = BUS_CAPTURE_VERSION;
    out[1] = (uint8_t)sizeof(out);
    store_be16(&out[2], state.count);
    store_be16(&out[4], state.capacity);
    store_be16(&out[6], state.head);
    out[8] = BUS_CAPTURE_MAX_DATA;
    out[9] = state.enabled ? 1u : 0u;
    store_be32(&out[10], state.seq);
    send_frame_port(g_last_rx_port, cmd | 0x80, out, (uint8_t)sizeof(out));
}

static void handle_bus_capture_read(const uint8_t *p, uint8_t len, uint8_t cmd)
{
    if (len < 3)
        return;
    uint16_t offset = load_be16(&p[0]);
    uint8_t want = p[2];
    if (want == 0 || want > 8)
        want = 8;

    uint8_t out[COMM_MAX_PAYLOAD];
    size_t pos = 1;
    uint8_t count = 0;

    bus_capture_state_t state;
    bus_capture_get_state(&state);
    if (offset < state.count)
    {
        uint16_t available = (uint16_t)(state.count - offset);
        uint8_t n = (available < want) ? (uint8_t)available : want;

        for (uint8_t i = 0; i < n; ++i)
        {
            bus_capture_record_t rec;
            if (!bus_capture_get_record((uint16_t)(offset + i), &rec))
                break;
            uint8_t rec_len = (uint8_t)(4u + rec.len);
            if ((pos + rec_len) > sizeof(out))
                break;
            store_be16(&out[pos], rec.dt_ms);
            out[pos + 2u] = rec.bus_id;
            out[pos + 3u] = rec.len;
            for (uint8_t j = 0; j < rec.len; ++j)
                out[pos + 4u + j] = rec.data[j];
            pos += rec_len;
            count++;
        }
    }

    out[0] = count;
    send_frame_port(g_last_rx_port, cmd | 0x80, out, (uint8_t)pos);
}

static void handle_bus_capture_control(const uint8_t *p, uint8_t len, uint8_t cmd)
{
    if (len < 1)
        return;
    uint8_t enable = p[0] ? 1u : 0u;
    uint8_t reset = 0;
    if (len >= 2)
        reset = p[1] ? 1u : 0u;
    bus_capture_set_enabled(enable, reset);
    send_status(cmd, 0);
}

static void handle_bus_capture_inject(const uint8_t *p, uint8_t len, uint8_t cmd)
{
    if (len < 4)
        return;
    uint8_t bus_id = p[0];
    uint16_t dt_ms = load_be16(&p[1]);
    uint8_t payload_len = p[3];
    if (len < (uint8_t)(4u + payload_len))
    {
        send_status(cmd, BUS_INJECT_STATUS_BAD_PAYLOAD);
        return;
    }
    if (!bus_capture_get_enabled())
    {
        bus_inject_log(BUS_INJECT_EVENT_BLOCKED_CAPTURE);
        send_status(cmd, BUS_INJECT_STATUS_CAPTURE_DISABLED);
        return;
    }

    uint8_t flags = 0;
    if (!bus_inject_allowed(&flags))
    {
        bus_inject_log(flags);
        if (flags & BUS_INJECT_EVENT_BLOCKED_ARMED)
            send_status(cmd, BUS_INJECT_STATUS_NOT_ARMED);
        else if (flags & BUS_INJECT_EVENT_BLOCKED_MODE)
            send_status(cmd, BUS_INJECT_STATUS_MODE);
        else if (flags & BUS_INJECT_EVENT_BLOCKED_MOVING)
            send_status(cmd, BUS_INJECT_STATUS_MOVING);
        else if (flags & BUS_INJECT_EVENT_BLOCKED_BRAKE)
            send_status(cmd, BUS_INJECT_STATUS_BRAKE);
        else
            send_status(cmd, BUS_INJECT_STATUS_BAD_RANGE);
        return;
    }

    flags |= BUS_INJECT_EVENT_OK;
    bus_inject_log(flags);
    bus_inject_emit(bus_id, &p[4], payload_len, dt_ms, flags);
    send_status(cmd, 0);
}

static void handle_bus_ui_control(const uint8_t *p, uint8_t len, uint8_t cmd)
{
    if (len < 1)
        return;
    uint8_t flags = p[0];
    bus_ui_state_t state;
    bus_ui_get_state(&state);
    uint8_t bus_id = state.filter_bus_id;
    uint8_t opcode = state.filter_opcode_val;
    if (len >= 2)
        bus_id = p[1];
    if (len >= 3)
        opcode = p[2];
    bus_ui_set_control(flags, bus_id, opcode);
    send_status(cmd, 0);
}

static void handle_bus_inject_arm(const uint8_t *p, uint8_t len, uint8_t cmd)
{
    if (len < 1)
        return;
    uint8_t armed = p[0] ? 1u : 0u;
    uint8_t override_flags = (len >= 2 && p[1]) ? (uint8_t)p[1] : 0u;
    bus_inject_set_armed(armed, override_flags);
    send_status(cmd, 0);
}

static void handle_bus_capture_replay(const uint8_t *p, uint8_t len, uint8_t cmd)
{
    if (len < 1)
        return;
    uint8_t mode = p[0];
    if (mode == 0)
    {
        bus_replay_cancel(BUS_INJECT_EVENT_BLOCKED_BRAKE);
        send_status(cmd, 0);
        return;
    }
    if (len < 4)
    {
        send_status(cmd, BUS_INJECT_STATUS_BAD_PAYLOAD);
        return;
    }
    uint8_t offset = p[1];
    uint16_t rate_ms = load_be16(&p[2]);
    if (!bus_capture_get_enabled())
    {
        bus_inject_log(BUS_INJECT_EVENT_BLOCKED_CAPTURE | BUS_INJECT_EVENT_REPLAY);
        send_status(cmd, BUS_INJECT_STATUS_CAPTURE_DISABLED);
        return;
    }
    uint8_t flags = 0;
    if (!bus_inject_allowed(&flags))
    {
        bus_inject_log((uint8_t)(flags | BUS_INJECT_EVENT_REPLAY));
        if (flags & BUS_INJECT_EVENT_BLOCKED_ARMED)
            send_status(cmd, BUS_INJECT_STATUS_NOT_ARMED);
        else if (flags & BUS_INJECT_EVENT_BLOCKED_MODE)
            send_status(cmd, BUS_INJECT_STATUS_MODE);
        else if (flags & BUS_INJECT_EVENT_BLOCKED_MOVING)
            send_status(cmd, BUS_INJECT_STATUS_MOVING);
        else if (flags & BUS_INJECT_EVENT_BLOCKED_BRAKE)
            send_status(cmd, BUS_INJECT_STATUS_BRAKE);
        else
            send_status(cmd, BUS_INJECT_STATUS_BAD_RANGE);
        return;
    }
    if (rate_ms < BUS_REPLAY_RATE_MIN_MS || rate_ms > BUS_REPLAY_RATE_MAX_MS)
    {
        send_status(cmd, BUS_INJECT_STATUS_BAD_RANGE);
        return;
    }
    bus_inject_log((uint8_t)(BUS_INJECT_EVENT_OK | BUS_INJECT_EVENT_REPLAY));
    bus_replay_start(offset, rate_ms);
    send_status(cmd, 0);
}

static void maybe_handle_gear_buttons(uint8_t buttons)
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
    (void)buttons;
}

static void handle_set_state(const uint8_t *p, uint8_t len, uint8_t cmd)
{
    if (len < 8)
        return;
    g_motor.rpm        = ((uint16_t)p[0] << 8) | p[1];
    g_motor.torque_raw = ((uint16_t)p[2] << 8) | p[3];
    g_motor.speed_dmph = ((uint16_t)p[4] << 8) | p[5];
    g_motor.soc_pct    = p[6];
    g_motor.err        = p[7];
    g_motor.last_ms    = g_ms;
    speed_rb_push(g_motor.speed_dmph);

    /* Mirror inputs into debug model (with optional extended fields). */
    g_inputs.speed_dmph   = g_motor.speed_dmph;
    g_inputs.torque_raw   = g_motor.torque_raw;
    g_inputs.cadence_rpm  = 0;
    g_inputs.power_w      = 0;
    g_inputs.battery_dV   = 0;
    g_inputs.battery_dA   = 0;
    g_inputs.ctrl_temp_dC = 0;
    g_inputs.throttle_pct = 0;
    g_inputs.brake        = 0;
    g_inputs.buttons      = 0;
    g_inputs.last_ms      = g_ms;
    g_input_caps          = 0;

    if (len >= 10)
        g_inputs.cadence_rpm = ((uint16_t)p[8] << 8) | p[9];
    if (len >= 11)
        g_inputs.throttle_pct = p[10];
    if (len >= 12)
        g_inputs.brake = p[11];
    if (len >= 13)
        g_inputs.buttons = p[12];
    if (len >= 15)
        g_inputs.power_w = ((uint16_t)p[13] << 8) | p[14];
    if (len >= 17)
    {
        g_inputs.battery_dV = (int16_t)(((uint16_t)p[15] << 8) | p[16]);
        g_input_caps |= INPUT_CAP_BATT_V;
    }
    if (len >= 19)
    {
        g_inputs.battery_dA = (int16_t)(((uint16_t)p[17] << 8) | p[18]);
        g_input_caps |= INPUT_CAP_BATT_I;
    }
    if (len >= 21)
    {
        g_inputs.ctrl_temp_dC = (int16_t)(((uint16_t)p[19] << 8) | p[20]);
        g_input_caps |= INPUT_CAP_TEMP;
    }

    graph_on_input_all();

    g_inputs_debug_last_ms = g_ms;
    process_buttons(g_inputs.buttons);

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
    maybe_handle_gear_buttons(g_inputs.buttons);
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

    send_status(cmd, 0);

}

static void handle_speed_rb_summary(uint8_t cmd)
{
    ringbuf_i16_summary_t s;
    speed_rb_summary(&s);
    uint8_t out[10];
    out[0] = s.count >> 8;
    out[1] = s.count & 0xFF;
    out[2] = s.capacity >> 8;
    out[3] = s.capacity & 0xFF;
    out[4] = (uint16_t)s.min >> 8;
    out[5] = (uint16_t)s.min & 0xFF;
    out[6] = (uint16_t)s.max >> 8;
    out[7] = (uint16_t)s.max & 0xFF;
    out[8] = (uint16_t)s.latest >> 8;
    out[9] = (uint16_t)s.latest & 0xFF;
    send_frame_port(g_last_rx_port, cmd | 0x80, out, (uint8_t)sizeof(out));
}

static void handle_graph_summary(uint8_t cmd)
{
    graph_summary_t summary;
    graph_get_active_summary(&summary);
    uint8_t out[14];
    store_be16(&out[0], summary.summary.count);
    store_be16(&out[2], summary.summary.capacity);
    store_be16(&out[4], (uint16_t)summary.summary.min);
    store_be16(&out[6], (uint16_t)summary.summary.max);
    store_be16(&out[8], (uint16_t)summary.summary.latest);
    store_be16(&out[10], summary.period_ms);
    store_be16(&out[12], summary.window_ms);
    send_frame_port(g_last_rx_port, cmd | 0x80, out, (uint8_t)sizeof(out));
}

static void handle_graph_control(const uint8_t *p, uint8_t len, uint8_t cmd)
{
    if (len < 2)
        return;
    uint8_t channel = p[0];
    uint8_t window = p[1];
    uint8_t reset = (len >= 3 && (p[2] & 0x01u)) ? 1u : 0u;
    if (!graph_set_active(channel, window, reset))
    {
        send_status(cmd, 0xFE);
        return;
    }
    send_status(cmd, 0);
}

static void fill_state_frame(comm_state_frame_t *state)
{
    if (!state)
        return;
    state->ms = g_ms;
    state->speed_dmph = g_inputs.speed_dmph;
    state->cadence_rpm = g_inputs.cadence_rpm;
    state->power_w = g_inputs.power_w;
    state->batt_dV = g_inputs.battery_dV;
    state->batt_dA = g_inputs.battery_dA;
    state->ctrl_temp_dC = g_inputs.ctrl_temp_dC;
    state->assist_mode = g_outputs.assist_mode;
    state->profile_id = g_outputs.profile_id;
    state->virtual_gear = g_outputs.virtual_gear;
    state->flags = (g_inputs.brake ? 0x01u : 0u) |
                   ((g_walk_state == WALK_STATE_ACTIVE) ? 0x02u : 0u);
}

void send_state_frame_bin(void)
{
    uint8_t out[COMM_STATE_FRAME_V1_LEN];
    comm_state_frame_t state;
    fill_state_frame(&state);
    uint8_t len = comm_state_frame_build_v1(out, (uint8_t)sizeof(out), &state);
    if (!len)
        return;
    send_frame_port(g_last_rx_port, 0x81, out, len); /* streaming telemetry frame */
}

static void handle_ble_hacker(const uint8_t *p, uint8_t len, uint8_t cmd)
{
    ble_hacker_frame_t req;
    uint8_t status = BLE_HACKER_STATUS_OK;
    uint8_t out[COMM_MAX_PAYLOAD];
    uint8_t resp_len = 0;

    if (!ble_hacker_decode(p, len, &req, &status))
    {
        resp_len = ble_hacker_encode_status((uint8_t)(BLE_HACKER_OP_ERROR | BLE_HACKER_OP_RESPONSE_FLAG),
                                            status, NULL, 0, out, (uint8_t)sizeof(out));
        if (resp_len)
            send_frame_port(g_last_rx_port, cmd | 0x80, out, resp_len);
        else
            send_status(cmd, 0xFE);
        return;
    }

    switch (req.opcode)
    {
    case BLE_HACKER_OP_VERSION:
    {
        uint8_t payload[3];
        payload[0] = BLE_HACKER_VERSION;
        payload[1] = (uint8_t)BLE_HACKER_MAX_PAYLOAD;
        payload[2] = BLE_HACKER_CAP_TELEMETRY | BLE_HACKER_CAP_CONFIG | BLE_HACKER_CAP_DEBUG;
        resp_len = ble_hacker_encode_status((uint8_t)(req.opcode | BLE_HACKER_OP_RESPONSE_FLAG),
                                            BLE_HACKER_STATUS_OK, payload, (uint8_t)sizeof(payload),
                                            out, (uint8_t)sizeof(out));
        break;
    }
    case BLE_HACKER_OP_TELEMETRY:
    {
        uint8_t telem[COMM_STATE_FRAME_V1_LEN];
        comm_state_frame_t state;
        fill_state_frame(&state);
        uint8_t tlen = comm_state_frame_build_v1(telem, (uint8_t)sizeof(telem), &state);
        if (!tlen)
            status = BLE_HACKER_STATUS_BAD_PAYLOAD;
        resp_len = ble_hacker_encode_status((uint8_t)(req.opcode | BLE_HACKER_OP_RESPONSE_FLAG),
                                            status,
                                            (status == BLE_HACKER_STATUS_OK) ? telem : NULL,
                                            (status == BLE_HACKER_STATUS_OK) ? tlen : 0,
                                            out, (uint8_t)sizeof(out));
        break;
    }
    case BLE_HACKER_OP_CONFIG_GET:
    {
        uint8_t cfg[CONFIG_BLOB_SIZE];
        config_store_be(cfg, &g_config_active);
        resp_len = ble_hacker_encode_status((uint8_t)(req.opcode | BLE_HACKER_OP_RESPONSE_FLAG),
                                            BLE_HACKER_STATUS_OK, cfg, (uint8_t)sizeof(cfg),
                                            out, (uint8_t)sizeof(out));
        break;
    }
    case BLE_HACKER_OP_CONFIG_STAGE:
    {
        if (!config_change_allowed())
        {
            status = BLE_HACKER_STATUS_BLOCKED;
            resp_len = ble_hacker_encode_status((uint8_t)(req.opcode | BLE_HACKER_OP_RESPONSE_FLAG),
                                                status, NULL, 0, out, (uint8_t)sizeof(out));
            break;
        }
        if (req.payload_len != CONFIG_BLOB_SIZE)
            status = BLE_HACKER_STATUS_BAD_PAYLOAD;
        else
            status = config_stage_blob(req.payload);
        resp_len = ble_hacker_encode_status((uint8_t)(req.opcode | BLE_HACKER_OP_RESPONSE_FLAG),
                                            status, NULL, 0, out, (uint8_t)sizeof(out));
        break;
    }
    case BLE_HACKER_OP_CONFIG_COMMIT:
    {
        if (!config_change_allowed())
        {
            status = BLE_HACKER_STATUS_BLOCKED;
            resp_len = ble_hacker_encode_status((uint8_t)(req.opcode | BLE_HACKER_OP_RESPONSE_FLAG),
                                                status, NULL, 0, out, (uint8_t)sizeof(out));
            break;
        }
        status = config_commit_staged(req.payload, req.payload_len);
        resp_len = ble_hacker_encode_status((uint8_t)(req.opcode | BLE_HACKER_OP_RESPONSE_FLAG),
                                            status, NULL, 0, out, (uint8_t)sizeof(out));
        break;
    }
    case BLE_HACKER_OP_DEBUG_LINE:
    {
        if (req.payload_len > 64u)
            status = BLE_HACKER_STATUS_BAD_PAYLOAD;
        resp_len = ble_hacker_encode_status((uint8_t)(req.opcode | BLE_HACKER_OP_RESPONSE_FLAG),
                                            status, NULL, 0, out, (uint8_t)sizeof(out));
        break;
    }
    default:
        resp_len = ble_hacker_encode_status((uint8_t)(req.opcode | BLE_HACKER_OP_RESPONSE_FLAG),
                                            BLE_HACKER_STATUS_BAD_OPCODE, NULL, 0,
                                            out, (uint8_t)sizeof(out));
        break;
    }

    if (!resp_len)
    {
        send_status(cmd, 0xFE);
        return;
    }
    send_frame_port(g_last_rx_port, cmd | 0x80, out, resp_len);
}

static void handle_set_stream(const uint8_t *p, uint8_t len, uint8_t cmd)
{
    if (len < 2)
        return;
    uint16_t period = ((uint16_t)p[0] << 8) | p[1];
    g_stream_period_ms = period;
    g_last_stream_ms = g_ms;
    send_status(cmd, 0);
}

static void handle_reboot_bootloader(uint8_t cmd)
{
    handle_set_bootloader_flag(cmd); /* ack + flag */
    reboot_to_bootloader();
}

int comm_handle_command(uint8_t cmd, const uint8_t *payload, uint8_t len)
{
    const uint8_t *p = payload;

    switch (cmd)
    {
    case 0x01: handle_ping(cmd); return 1;
    case LOG_FRAME_CMD: handle_log_frame(cmd); return 1;
    case 0x02: handle_read32(p, len, cmd); return 1;
    case 0x03: handle_write32(p, len, cmd); return 1;
    case 0x04: handle_read_mem(p, len, cmd); return 1;
    case 0x05: handle_write_mem(p, len, cmd); return 1;
    case 0x06: handle_exec(p, len, cmd); return 1;
    case 0x07: handle_upload_exec(p, len, cmd); return 1;
    case 0x08: handle_read_flash(p, len, cmd); return 1;
    case 0x0A: handle_state_dump(cmd); return 1;
    case 0x0B: handle_set_bootloader_flag(cmd); return 1;
    case 0x0C: handle_set_state(p, len, cmd); return 1;
    case 0x0D: handle_set_stream(p, len, cmd); return 1;
    case 0x0E: handle_reboot_bootloader(cmd); return 1;
    case 0x20: handle_speed_rb_summary(cmd); return 1;
    case 0x21: handle_debug_state_v2(cmd); return 1;
    case 0x22: handle_graph_summary(cmd); return 1;
    case 0x23: handle_graph_control(p, len, cmd); return 1;
    case 0x30: handle_config_get(cmd); return 1;
    case 0x31: handle_config_stage(p, len, cmd); return 1;
    case 0x32: handle_config_commit(p, len, cmd); return 1;
    case 0x33: handle_set_profile(p, len, cmd); return 1;
    case 0x34: handle_set_gears(p, len, cmd); return 1;
    case 0x35: handle_set_cadence_bias(p, len, cmd); return 1;
    case 0x36: handle_trip_get(cmd); return 1;
    case 0x37: handle_trip_reset(cmd); return 1;
    case 0x38: handle_set_drive_mode(p, len, cmd); return 1;
    case 0x39: handle_set_regen(p, len, cmd); return 1;
    case 0x3A: handle_set_hw_caps(p, len, cmd); return 1;
    case 0x40: handle_event_log_summary(cmd); return 1;
    case 0x41: handle_event_log_read(p, len, cmd); return 1;
    case 0x42: handle_event_log_mark(p, len, cmd); return 1;
    case 0x44: handle_stream_log_summary(cmd); return 1;
    case 0x45: handle_stream_log_read(p, len, cmd); return 1;
    case 0x46: handle_stream_log_control(p, len, cmd); return 1;
    case 0x47: handle_crash_dump_read(cmd); return 1;
    case 0x48: handle_crash_dump_clear(cmd); return 1;
    case 0x50: handle_bus_capture_summary(cmd); return 1;
    case 0x51: handle_bus_capture_read(p, len, cmd); return 1;
    case 0x52: handle_bus_capture_control(p, len, cmd); return 1;
    case 0x53: handle_bus_capture_inject(p, len, cmd); return 1;
    case 0x54: handle_bus_ui_control(p, len, cmd); return 1;
    case 0x55: handle_bus_inject_arm(p, len, cmd); return 1;
    case 0x56: handle_bus_capture_replay(p, len, cmd); return 1;
    case 0x71: handle_ab_status(cmd); return 1;
    case 0x72: handle_ab_set_pending(p, len, cmd); return 1;
    case 0x70: handle_ble_hacker(p, len, cmd); return 1;
    default:
        return 0;
    }
}
