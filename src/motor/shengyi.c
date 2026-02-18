/*
 * Shengyi DWG22 Motor Protocol (custom variant)
 *
 * Handles communication with the Shengyi DWG22 motor controller via UART2.
 * Implements the 0x52 status request frame and OEM assist level mapping.
 */

#include "shengyi.h"
#include "../control/control.h"
#include "../power/power.h"
#include "battery_soc.h"
#include "app_data.h"
#include "../config/config.h"
#include "motor_isr.h"
#include "../../drivers/uart.h"
#include "../../platform/hw.h"
#include "../util/bool_to_u8.h"

extern volatile uint32_t g_ms;

/* Module state */
static uint8_t g_shengyi_req_pending;
static uint8_t g_shengyi_req_force;
static uint8_t g_shengyi_last_assist;
static uint8_t g_shengyi_last_flags;
static uint8_t g_shengyi_handshake_ok;
static uint32_t g_shengyi_cfg_last_ms;
static uint32_t g_shengyi_speed_last_ms;
static uint32_t g_shengyi_status_last_ms;
static uint16_t g_shengyi_speed_target_dmph;
static uint16_t g_shengyi_speed_smoothed_dmph;
static uint16_t g_shengyi_speed_step_dmph;

#define SHENGYI_CFG_INTERVAL_MS 500u
#define SHENGYI_STATUS_INTERVAL_MS 100u
#define SHENGYI_SPEED_SMOOTH_MS 100u

typedef struct {
    uint8_t n5;
    uint8_t n10;
    uint8_t n48;
    uint8_t n5_0;
    uint8_t byte_de6;
    uint8_t n2_2;
    uint8_t n64;
    uint8_t byte_dea;
    uint8_t byte_de9;
    uint8_t n2_3;
    uint8_t byte_de5;
    uint16_t word_dee;
    uint16_t n15000;
    uint16_t n320;
    uint8_t n12;
    uint16_t n290;
    uint16_t n2355;
    uint8_t byte_e21;
    uint16_t n10000;
    uint8_t n3;
    uint8_t n10_1;
    uint8_t byte_e0f;
    uint8_t n3_2;
    uint8_t n2_0;
    uint8_t byte_e11;
    uint8_t n5_4;
    uint8_t n2_1;
    uint8_t byte_e13;
    uint8_t byte_e14;
    uint8_t byte_e15;
    uint8_t byte_e02;
    uint16_t word_de0;
    uint16_t word_dde;
    uint8_t byte_de2;
    uint8_t n6_0;
    uint16_t n750;
    uint16_t n7200;
    uint16_t n260;
    uint8_t n40;
    uint8_t n65;
    uint8_t n49;
} shengyi_oem_config_t;

static shengyi_oem_config_t g_shengyi_cfg;

static uint16_t shengyi_kph_x10_to_dmph(uint16_t kph_x10)
{
    uint32_t dmph = ((uint32_t)kph_x10 * 62137u + 50000u) / 100000u;
    if (dmph > 65535u)
        dmph = 65535u;
    return (uint16_t)dmph;
}

static void shengyi_apply_oem_limits(void)
{
    g_config_active.cap_current_dA = (uint16_t)((g_shengyi_cfg.n15000 + 50u) / 100u);
    g_config_active.cap_speed_dmph = shengyi_kph_x10_to_dmph(g_shengyi_cfg.n320);
    if (g_shengyi_cfg.n2355)
        g_config_active.wheel_mm = g_shengyi_cfg.n2355;
}

void shengyi_notify_rx_opcode(uint8_t opcode)
{
    if (opcode == SHENGYI_OPCODE_CONFIG_53)
        g_shengyi_handshake_ok = 1u;
}

uint8_t shengyi_handshake_ok(void)
{
    return bool_to_u8(g_shengyi_handshake_ok);
}

void shengyi_speed_update_target(uint16_t speed_dmph)
{
    g_shengyi_speed_target_dmph = speed_dmph;
    if (g_shengyi_speed_target_dmph < g_shengyi_speed_smoothed_dmph) {
        g_shengyi_speed_step_dmph = (uint16_t)((g_shengyi_speed_smoothed_dmph - g_shengyi_speed_target_dmph) / 5u);
        if (g_shengyi_speed_step_dmph == 0u && g_shengyi_speed_target_dmph == 0u)
            g_shengyi_speed_smoothed_dmph = 0u;
    } else {
        g_shengyi_speed_step_dmph = (uint16_t)((g_shengyi_speed_target_dmph - g_shengyi_speed_smoothed_dmph) / 5u);
    }
}

static void shengyi_speed_smooth_tick(uint32_t now_ms)
{
    if ((uint32_t)(now_ms - g_shengyi_speed_last_ms) < SHENGYI_SPEED_SMOOTH_MS)
        return;
    g_shengyi_speed_last_ms = now_ms;

    if (g_shengyi_speed_target_dmph <= g_shengyi_speed_smoothed_dmph) {
        if (g_shengyi_speed_smoothed_dmph < g_shengyi_speed_step_dmph)
            g_shengyi_speed_smoothed_dmph = 0u;
        else
            g_shengyi_speed_smoothed_dmph = (uint16_t)(g_shengyi_speed_smoothed_dmph - g_shengyi_speed_step_dmph);
    } else {
        g_shengyi_speed_smoothed_dmph = (uint16_t)(g_shengyi_speed_smoothed_dmph + g_shengyi_speed_step_dmph);
        if (g_shengyi_speed_smoothed_dmph >= g_shengyi_speed_target_dmph)
            g_shengyi_speed_smoothed_dmph = g_shengyi_speed_target_dmph;
    }
}

static uint8_t shengyi_oem_wheel_code(uint16_t n290)
{
    switch (n290) {
        case 160: return 0;
        case 180: return 1;
        case 200: return 2;
        case 220: return 3;
        case 240: return 4;
        case 260: return 5;
        case 275: return 6;
        case 280: return 7;
        case 290: return 7;
        default:  return 0;
    }
}

static void shengyi_oem_apply_wheel_code(uint8_t code)
{
    switch (code) {
        case 0:
            g_shengyi_cfg.n12 = 3;
            g_shengyi_cfg.n290 = 160;
            g_shengyi_cfg.n2355 = 1276;
            break;
        case 1:
            g_shengyi_cfg.n12 = 4;
            g_shengyi_cfg.n290 = 180;
            g_shengyi_cfg.n2355 = 1436;
            break;
        case 2:
            g_shengyi_cfg.n12 = 5;
            g_shengyi_cfg.n290 = 200;
            g_shengyi_cfg.n2355 = 1595;
            break;
        case 3:
            g_shengyi_cfg.n12 = 6;
            g_shengyi_cfg.n290 = 220;
            g_shengyi_cfg.n2355 = 1755;
            break;
        case 4:
            g_shengyi_cfg.n12 = 7;
            g_shengyi_cfg.n290 = 240;
            g_shengyi_cfg.n2355 = 1914;
            break;
        case 5:
            g_shengyi_cfg.n12 = 8;
            g_shengyi_cfg.n290 = 260;
            g_shengyi_cfg.n2355 = 2074;
            break;
        case 6:
            g_shengyi_cfg.n12 = 13;
            g_shengyi_cfg.n290 = 275;
            g_shengyi_cfg.n2355 = 2193;
            break;
        case 7:
            g_shengyi_cfg.n290 = 290;
            g_shengyi_cfg.n2355 = 2313;
            break;
        default:
            break;
    }
}

static void shengyi_oem_config_defaults(void)
{
    g_shengyi_cfg.n5 = 5u;
    g_shengyi_cfg.n10 = 10u;
    g_shengyi_cfg.n48 = 48u;
    g_shengyi_cfg.n5_0 = 5u;
    g_shengyi_cfg.byte_de6 = 0u;
    g_shengyi_cfg.n2_2 = 2u;
    g_shengyi_cfg.n64 = 64u;
    g_shengyi_cfg.byte_dea = 0u;
    g_shengyi_cfg.byte_de9 = 0u;
    g_shengyi_cfg.n2_3 = 2u;
    g_shengyi_cfg.byte_de5 = 1u;
    g_shengyi_cfg.word_dee = 0xA410u;
    g_shengyi_cfg.n15000 = 15000u;
    g_shengyi_cfg.n320 = 320u;
    g_shengyi_cfg.n12 = 12u;
    g_shengyi_cfg.n290 = 290u;
    g_shengyi_cfg.n2355 = 2355u;
    g_shengyi_cfg.byte_e21 = 1u;
    g_shengyi_cfg.n10000 = 10000u;
    g_shengyi_cfg.n3 = 3u;
    g_shengyi_cfg.n10_1 = 1u;
    g_shengyi_cfg.byte_e0f = 1u;
    g_shengyi_cfg.n3_2 = 3u;
    g_shengyi_cfg.n2_0 = 2u;
    g_shengyi_cfg.byte_e11 = 1u;
    g_shengyi_cfg.n5_4 = 0u;
    g_shengyi_cfg.n2_1 = 2u;
    g_shengyi_cfg.byte_e13 = 0u;
    g_shengyi_cfg.byte_e14 = 1u;
    g_shengyi_cfg.byte_e15 = 0u;
    g_shengyi_cfg.byte_e02 = 1u;
    g_shengyi_cfg.word_de0 = 0u;
    g_shengyi_cfg.word_dde = 0u;
    g_shengyi_cfg.byte_de2 = 1u;
    g_shengyi_cfg.n6_0 = 6u;
    g_shengyi_cfg.n750 = 750u;
    g_shengyi_cfg.n7200 = 7200u;
    g_shengyi_cfg.n260 = 260u;
    g_shengyi_cfg.n40 = 40u;
    g_shengyi_cfg.n65 = 65u;
    g_shengyi_cfg.n49 = 49u;
    shengyi_apply_oem_limits();
}

uint8_t shengyi_nominal_v(void)
{
    uint8_t v = g_shengyi_cfg.n48;
    return (v == 24u || v == 36u || v == 48u) ? v : 0u;
}

uint8_t shengyi_batt_soc_pct_from_dV(int16_t batt_dV)
{
    if (batt_dV <= 0)
        return 0u;
    uint32_t batt_mv = (uint32_t)batt_dV * 100u; /* 0.1V -> mV */
    return battery_soc_pct_from_mv(batt_mv, shengyi_nominal_v());
}

static uint8_t shengyi_battery_low_flag(void)
{
    return bool_to_u8((g_input_caps & INPUT_CAP_BATT_V) &&
                      shengyi_batt_soc_pct_from_dV(g_inputs.battery_dV) == 0u);
}

/* -------------------------------------------------------------
 * Build flags byte from current state
 * ------------------------------------------------------------- */
uint8_t shengyi_build_flags(void)
{
    uint8_t flags = 0;
    if (g_headlight_enabled)
        flags |= SHENGYI_FLAG_LIGHT;
    if (shengyi_battery_low_flag())
        flags |= SHENGYI_FLAG_BATTLOW;
    if (g_walk_state == WALK_STATE_ACTIVE)
        flags |= SHENGYI_FLAG_WALK;
    if (g_effective_cap_speed_dmph && g_shengyi_speed_smoothed_dmph > g_effective_cap_speed_dmph)
        flags |= SHENGYI_FLAG_SPEED;
    return flags;
}

/* -------------------------------------------------------------
 * Send a 0x52 request frame
 * ------------------------------------------------------------- */
void shengyi_send_0x52_req(uint8_t assist_level_mapped,
                              uint8_t headlight_enabled,
                              uint8_t walk_assist_active,
                              uint8_t battery_low,
                              uint8_t speed_over_limit)
{
    motor_isr_queue_cmd(
        assist_level_mapped,
        bool_to_u8(headlight_enabled),
        bool_to_u8(walk_assist_active),
        bool_to_u8(battery_low),
        bool_to_u8(speed_over_limit));
}

/* -------------------------------------------------------------
 * Request update to motor
 * ------------------------------------------------------------- */
void shengyi_request_update(uint8_t force)
{
    uint8_t assist = shengyi_assist_level_mapped();
    uint8_t flags = shengyi_build_flags();
    if (force || assist != g_shengyi_last_assist || flags != g_shengyi_last_flags)
    {
        g_shengyi_req_pending = 1u;
        if (force)
            g_shengyi_req_force = 1u;
    }
}

/* -------------------------------------------------------------
 * Periodic send tick
 * ------------------------------------------------------------- */
void shengyi_periodic_send_tick(void)
{
    shengyi_speed_smooth_tick(g_ms);
    uint32_t now_ms = g_ms;

    if (!g_shengyi_handshake_ok) {
        if ((uint32_t)(now_ms - g_shengyi_cfg_last_ms) >= SHENGYI_CFG_INTERVAL_MS || g_shengyi_req_force) {
            uint8_t frame[16];
            size_t len = shengyi_build_frame_0x53(frame, sizeof(frame));
            if (len)
                motor_isr_queue_frame(frame, (uint8_t)len);
            g_shengyi_cfg_last_ms = now_ms;
            g_shengyi_req_pending = 0u;
            g_shengyi_req_force = 0u;
            return;
        }
        if ((uint32_t)(now_ms - g_shengyi_status_last_ms) >= SHENGYI_STATUS_INTERVAL_MS) {
            shengyi_send_0x52_req(0u, 0u, 0u, 0u, 0u);
            g_shengyi_status_last_ms = now_ms;
        }
        return;
    }

    if (!g_shengyi_req_pending && !g_shengyi_req_force &&
        (uint32_t)(now_ms - g_shengyi_status_last_ms) < SHENGYI_STATUS_INTERVAL_MS)
        return;
    uint8_t assist = shengyi_assist_level_mapped();
    uint8_t flags = shengyi_build_flags();
    uint8_t headlight = bool_to_u8((flags & SHENGYI_FLAG_LIGHT) != 0u);
    uint8_t battery_low = bool_to_u8((flags & SHENGYI_FLAG_BATTLOW) != 0u);
    uint8_t walk = bool_to_u8((flags & SHENGYI_FLAG_WALK) != 0u);
    uint8_t speed_over = bool_to_u8((flags & SHENGYI_FLAG_SPEED) != 0u);
    shengyi_send_0x52_req(assist, headlight, walk, battery_low, speed_over);
    g_shengyi_last_assist = assist;
    g_shengyi_last_flags = flags;
    g_shengyi_status_last_ms = now_ms;
    g_shengyi_req_pending = 0u;
    g_shengyi_req_force = 0u;
}

/* -------------------------------------------------------------
 * OEM assist level mapping
 *
 * The Shengyi DWG22 variant only supports certain assist level counts
 * (1, 3, 5, 6, 9). This maps virtual gear count to the closest.
 * ------------------------------------------------------------- */
uint8_t shengyi_assist_oem_max(uint8_t count)
{
    static const uint8_t opts[] = {1u, 3u, 5u, 6u, 9u};
    uint8_t best = opts[0];
    uint8_t best_diff = (count > best) ? (uint8_t)(count - best) : (uint8_t)(best - count);
    for (size_t i = 1; i < (sizeof(opts) / sizeof(opts[0])); ++i)
    {
        uint8_t v = opts[i];
        uint8_t diff = (count > v) ? (uint8_t)(count - v) : (uint8_t)(v - count);
        if (diff < best_diff || (diff == best_diff && v > best))
        {
            best = v;
            best_diff = diff;
        }
    }
    return best;
}

uint8_t shengyi_assist_oem_lut(uint8_t max, uint8_t idx)
{
    if (idx == 11u)
        return 0x32u;
    switch (max)
    {
        case 1:
        {
            static const uint8_t lut[] = {0u, 0x66u};
            if (idx < (uint8_t)(sizeof(lut) / sizeof(lut[0])))
                return lut[idx];
            break;
        }
        case 3:
        {
            static const uint8_t lut[] = {0u, 0x66u, 0x8Cu, 0xB3u};
            if (idx < (uint8_t)(sizeof(lut) / sizeof(lut[0])))
                return lut[idx];
            break;
        }
        case 5:
        {
            static const uint8_t lut[] = {0u, 0x66u, 0x8Cu, 0xB3u, 0xD9u, 0xFFu};
            if (idx < (uint8_t)(sizeof(lut) / sizeof(lut[0])))
                return lut[idx];
            break;
        }
        case 6:
        {
            static const uint8_t lut[] = {0u, 0x66u, 0x84u, 0xA2u, 0xC0u, 0xDEu, 0xFFu};
            if (idx < (uint8_t)(sizeof(lut) / sizeof(lut[0])))
                return lut[idx];
            break;
        }
        case 9:
        {
            static const uint8_t lut[] = {
                0u, 0x66u, 0x79u, 0x89u, 0x9Cu, 0xAFu, 0xC2u, 0xD5u, 0xE8u, 0xFFu
            };
            if (idx < (uint8_t)(sizeof(lut) / sizeof(lut[0])))
                return lut[idx];
            break;
        }
        default:
            break;
    }
    return 0u;
}

uint8_t shengyi_assist_level_mapped(void)
{
    uint8_t max = shengyi_assist_oem_max(g_vgears.count);
    if (g_inputs.brake)
        return 0u;
    uint8_t idx = g_active_vgear;
    if (idx == 0u)
        idx = 1u;
    if (idx > max)
        idx = max;
    if (g_walk_state == WALK_STATE_ACTIVE)
        idx = 11u;
    return shengyi_assist_oem_lut(max, idx);
}

/* -------------------------------------------------------------
 * OEM config handling (0xC0/0xC2/0xC3)
 * ------------------------------------------------------------- */
int shengyi_apply_config_c0(const uint8_t *payload, uint8_t len)
{
    if (!payload || len < 52u)
        return 0;

    if (payload[0] >= 1u && payload[0] <= 5u)
        g_shengyi_cfg.n5 = payload[0];
    if (payload[1] <= 0x0Au)
        g_shengyi_cfg.n10 = payload[1];

    if (payload[7] == 24u || payload[7] == 36u || payload[7] == 48u)
        g_shengyi_cfg.n48 = payload[7];

    if (payload[8] == 3u || payload[8] == 5u || payload[8] == 9u) {
        g_shengyi_cfg.n5_0 = payload[8];
        g_vgears.count = payload[8];
        vgear_generate_scales(&g_vgears);
        if (g_active_vgear == 0u || g_active_vgear > g_vgears.count)
            g_active_vgear = g_vgears.count;
    }

    g_shengyi_cfg.byte_de6 = payload[9];
    if (payload[10] >= 2u && payload[10] <= 0x40u)
        g_shengyi_cfg.n2_2 = payload[10];
    g_shengyi_cfg.n64 = payload[11];
    g_shengyi_cfg.byte_dea = payload[12];
    g_shengyi_cfg.byte_de9 = payload[13];
    if (payload[14] <= 3u)
        g_shengyi_cfg.n2_3 = payload[14];
    if (payload[15] <= 0x0Fu)
        g_shengyi_cfg.byte_de5 = payload[15];

    g_shengyi_cfg.word_dee = (uint16_t)((uint16_t)payload[16] << 8) | payload[17];
    g_shengyi_cfg.n15000 = (uint16_t)payload[18] * 1000u;
    if (payload[19] >= 0x0Au && payload[19] <= 0x33u)
        g_shengyi_cfg.n320 = (uint16_t)payload[19] * 10u;

    shengyi_oem_apply_wheel_code(payload[20]);

    if (payload[21] && payload[21] <= 0x3Cu)
        g_shengyi_cfg.byte_e21 = payload[21];
    if (payload[22] >= 5u)
        g_shengyi_cfg.n10000 = (uint16_t)payload[22] * 1000u;
    if (payload[23] <= 0x0Au)
        g_shengyi_cfg.n3 = payload[23];
    g_shengyi_cfg.n10_1 = bool_to_u8(payload[24]);
    g_shengyi_cfg.byte_e0f = bool_to_u8(payload[25]);
    g_shengyi_cfg.n2355 = (uint16_t)((uint16_t)payload[26] << 8) | payload[27];
    g_shengyi_cfg.n3_2 = payload[28];
    g_shengyi_cfg.n2_0 = payload[29];
    g_shengyi_cfg.byte_e11 = payload[30];
    g_shengyi_cfg.n5_4 = payload[31];
    g_shengyi_cfg.n2_1 = payload[32];
    g_shengyi_cfg.byte_e13 = payload[33];
    g_shengyi_cfg.byte_e14 = payload[34];
    g_shengyi_cfg.byte_e15 = payload[35];
    g_shengyi_cfg.byte_e02 = payload[36];
    g_shengyi_cfg.word_de0 = (uint16_t)((uint16_t)payload[37] << 8) | payload[38];
    g_shengyi_cfg.word_dde = (uint16_t)((uint16_t)payload[39] << 8) | payload[40];
    g_shengyi_cfg.byte_de2 = payload[41];
    g_shengyi_cfg.n6_0 = payload[42];
    g_shengyi_cfg.n750 = (uint16_t)((uint16_t)payload[43] << 8) | payload[44];
    g_shengyi_cfg.n7200 = (uint16_t)((uint16_t)payload[45] << 8) | payload[46];
    g_shengyi_cfg.n260 = (uint16_t)((uint16_t)payload[47] << 8) | payload[48];
    g_shengyi_cfg.n40 = payload[49];
    g_shengyi_cfg.n65 = payload[50];
    g_shengyi_cfg.n49 = payload[51];

    shengyi_apply_oem_limits();
    return 1;
}

size_t shengyi_build_frame_0xC1(uint8_t status, uint8_t *out, size_t cap)
{
    uint8_t payload[1];
    payload[0] = status;
    return shengyi_frame_build(0xC1u, payload, 1u, out, cap);
}

size_t shengyi_build_frame_0xC3(uint8_t *out, size_t cap)
{
    uint8_t payload[47];
    payload[0] = g_shengyi_cfg.n5;
    payload[1] = g_shengyi_cfg.n10;
    payload[2] = g_shengyi_cfg.n48;
    payload[3] = g_shengyi_cfg.n5_0;
    payload[4] = g_shengyi_cfg.byte_de6;
    payload[5] = g_shengyi_cfg.n2_2;
    payload[6] = g_shengyi_cfg.n64;
    payload[7] = g_shengyi_cfg.byte_dea;
    payload[8] = g_shengyi_cfg.byte_de9;
    payload[9] = g_shengyi_cfg.n2_3;
    payload[10] = g_shengyi_cfg.byte_de5;
    payload[11] = (uint8_t)(g_shengyi_cfg.word_dee >> 8);
    payload[12] = (uint8_t)(g_shengyi_cfg.word_dee & 0xFFu);
    payload[13] = (uint8_t)(g_shengyi_cfg.n15000 / 1000u);
    payload[14] = (uint8_t)(g_shengyi_cfg.n320 / 10u);
    payload[15] = shengyi_oem_wheel_code(g_shengyi_cfg.n290);
    payload[16] = g_shengyi_cfg.byte_e21;
    payload[17] = (uint8_t)(g_shengyi_cfg.n10000 / 1000u);
    payload[18] = g_shengyi_cfg.n3;
    payload[19] = bool_to_u8(g_shengyi_cfg.n10_1);
    payload[20] = bool_to_u8(g_shengyi_cfg.byte_e0f);
    payload[21] = (uint8_t)(g_shengyi_cfg.n2355 >> 8);
    payload[22] = (uint8_t)(g_shengyi_cfg.n2355 & 0xFFu);
    payload[23] = g_shengyi_cfg.n3_2;
    payload[24] = g_shengyi_cfg.n2_0;
    payload[25] = g_shengyi_cfg.byte_e11;
    payload[26] = g_shengyi_cfg.n5_4;
    payload[27] = g_shengyi_cfg.n2_1;
    payload[28] = g_shengyi_cfg.byte_e13;
    payload[29] = g_shengyi_cfg.byte_e14;
    payload[30] = g_shengyi_cfg.byte_e15;
    payload[31] = g_shengyi_cfg.byte_e02;
    payload[32] = (uint8_t)(g_shengyi_cfg.word_de0 >> 8);
    payload[33] = (uint8_t)(g_shengyi_cfg.word_de0 & 0xFFu);
    payload[34] = (uint8_t)(g_shengyi_cfg.word_dde >> 8);
    payload[35] = (uint8_t)(g_shengyi_cfg.word_dde & 0xFFu);
    payload[36] = 1u;
    payload[37] = g_shengyi_cfg.n6_0;
    payload[38] = (uint8_t)(g_shengyi_cfg.n750 >> 8);
    payload[39] = (uint8_t)(g_shengyi_cfg.n750 & 0xFFu);
    payload[40] = (uint8_t)(g_shengyi_cfg.n7200 >> 8);
    payload[41] = (uint8_t)(g_shengyi_cfg.n7200 & 0xFFu);
    payload[42] = (uint8_t)(g_shengyi_cfg.n260 >> 8);
    payload[43] = (uint8_t)(g_shengyi_cfg.n260 & 0xFFu);
    payload[44] = g_shengyi_cfg.n40;
    payload[45] = g_shengyi_cfg.n65;
    payload[46] = g_shengyi_cfg.n49;
    return shengyi_frame_build(0xC3u, payload, (uint8_t)sizeof(payload), out, cap);
}

size_t shengyi_build_frame_0x53(uint8_t *out, size_t cap)
{
    uint8_t payload[7];
    uint8_t b0 = (uint8_t)(g_shengyi_cfg.n2_2 & 0x3Fu);
    if (g_shengyi_cfg.byte_de6)
        b0 &= ~0x40u;
    else
        b0 |= 0x40u;
    payload[0] = b0;
    payload[1] = g_shengyi_cfg.n64;
    payload[2] = (uint8_t)((g_shengyi_cfg.byte_dea ? 0x80u : 0u)
                        | (g_shengyi_cfg.byte_de9 ? 0x40u : 0u)
                        | ((g_shengyi_cfg.n2_3 & 0x3u) << 4)
                        | (g_shengyi_cfg.byte_de5 & 0x0Fu));

    uint16_t v = (uint16_t)(g_shengyi_cfg.word_dee / 100u);
    uint8_t b27;
    uint8_t b28_top;
    if (v > 0x106u) {
        if (v >= 0x170u) {
            b28_top = 0xC0u;
            if (v >= 0x1A4u)
                b27 = (uint8_t)(v + 92u);
            else
                b27 = (uint8_t)(0x80u | (uint8_t)(164u - v));
        } else {
            b28_top = 0x80u;
            if (v > 0x13Au)
                b27 = (uint8_t)(v - 59u);
            else
                b27 = (uint8_t)(0x80u | (uint8_t)(315u - v));
        }
    } else {
        b28_top = 0x40u;
        if (v >= 0xD2u)
            b27 = (uint8_t)(v + 46u);
        else
            b27 = (uint8_t)(0x80u | (uint8_t)(210u - v));
    }

    uint8_t current = (uint8_t)(2u * (g_shengyi_cfg.n15000 / 1000u));
    uint8_t b28 = (uint8_t)((b28_top & 0xC0u) | (current & 0x3Fu));
    payload[3] = b27;
    payload[4] = b28;
    payload[5] = 2u;

    uint16_t kph = (uint16_t)(g_shengyi_cfg.n320 / 10u);
    if (kph < 10u)
        kph = 10u;
    if (kph > 51u)
        kph = 51u;
    uint8_t speed_bits = (uint8_t)((kph - 10u) & 0x1Fu);
    payload[6] = (uint8_t)((shengyi_oem_wheel_code(g_shengyi_cfg.n290) & 0x07u)
                        | (uint8_t)(speed_bits << 3));

    return shengyi_frame_build(0x53u, payload, (uint8_t)sizeof(payload), out, cap);
}

/* -------------------------------------------------------------
 * Init/tick stubs (full implementation in main.c for now)
 * ------------------------------------------------------------- */
void shengyi_init(void)
{
    g_shengyi_req_pending = 0;
    g_shengyi_req_force = 0;
    g_shengyi_last_assist = 0;
    g_shengyi_last_flags = 0;
    g_shengyi_handshake_ok = 0;
    g_shengyi_cfg_last_ms = 0;
    g_shengyi_speed_last_ms = 0;
    g_shengyi_status_last_ms = 0;
    g_shengyi_speed_target_dmph = 0;
    g_shengyi_speed_smoothed_dmph = 0;
    g_shengyi_speed_step_dmph = 0;
    shengyi_oem_config_defaults();
}

void shengyi_tick(void)
{
    shengyi_periodic_send_tick();
}
