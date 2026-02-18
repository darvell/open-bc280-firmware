/*
 * Motor Link Manager (main-loop side)
 *
 * Drives per-protocol periodic TX and provides runtime protocol selection.
 *
 * Important:
 * - TX bytes are queued into motor_isr for ISR-timed emission.
 * - RX parsing runs in motor_isr; we only observe the last-frame snapshot here.
 */

#include "motor_link.h"
#include "../util/bool_to_u8.h"

#include <string.h>

#include "app_data.h"
#include "shengyi.h"
#include "motor_isr.h"

#include "src/control/control.h"
#include "src/config/config.h"

#ifndef HOST_TEST
#include "../../platform/time.h"
#include "../../platform/hw.h"
#include "../../platform/clock.h"
#include "../../drivers/uart.h"
#else
extern volatile uint32_t g_ms;
#endif

/* OEM-mode-ish send cadences (best-effort). */
#define PROBE_INTERVAL_MS   200u
#define STX02_INTERVAL_MS   100u
#define AUTH_INTERVAL_MS    250u
#define V2_INTERVAL_MS      120u

/* Protocol helpers and wire defaults. */
#define MOTOR_LINK_DEFAULT_WHEEL_MM SHENGYI_DEFAULT_WHEEL_MM
#define MOTOR_LINK_PROTO_SLOT_COUNT 4u
#define MOTOR_LINK_STX02_FRAME_LEN  20u
#define MOTOR_LINK_STX02_FRAME_LIMIT_KPH_X10 510u
#define MOTOR_LINK_STX02_BATT_THRESHOLD_MV 42000u
#define MOTOR_LINK_V2_SPEED_LIMIT_CODE 0x01FEu

/* V2 protocol uses 1200 baud; everything else uses 9600. */
#define MOTOR_BAUD_DEFAULT  9600u
#define MOTOR_BAUD_V2       1200u

static struct {
    motor_link_mode_t mode;
    uint8_t locked;
    motor_proto_t locked_proto;

    uint8_t proto_score[MOTOR_LINK_PROTO_SLOT_COUNT];
    uint8_t last_seq_seen;

    uint32_t last_probe_ms;
    uint8_t probe_step;

    uint32_t last_stx02_ms;
    uint32_t last_auth_ms;
    uint8_t auth_phase;

    uint32_t last_v2_ms;
    uint8_t v2_step;

    /* Protocol B (STX02/XOR) OEM-ish state.
     * Mirrors the OEM app's STX02-related globals, but kept local until fully mapped. */
    uint8_t stx02_bit6_src;           /* OEM byte_20001E55 & 1, default 0 */
    uint8_t stx02_bit3_src;           /* OEM byte_20001E56 & 1, default 1 */
    uint8_t stx02_speed_gate;         /* OEM byte_20001E65, default 0 */
    uint8_t stx02_pulse_req;          /* OEM byte_20001DA4 one-shot (only used when stx02_bit3_src==0) */
    uint8_t stx02_last_walk_active;   /* for edge-detecting walk transitions */
    uint16_t stx02_speed_filt_kph_x10; /* OEM word_20001DAC (filtered speed, kph*10) */

    uint32_t pclk1_hz;               /* cached APB1 clock for BRR computation */
    uint32_t current_baud;            /* last baud rate set on UART2 */
} g_motor_link;

static uint16_t dmph_to_kph_x10(uint16_t dmph);
static uint16_t stx02_speed_filter_update(uint16_t target_kph_x10);
static void stx02_refresh_opts_from_config(void);
static bool motor_link_send_slot_due(uint32_t now_ms, uint32_t *last_ms, uint32_t interval_ms);
static void motor_link_reset_mode_state(void);
static uint16_t motor_link_effective_wheel_mm(void);

static void motor_link_set_baud(uint32_t baud)
{
#ifndef HOST_TEST
    if (baud == g_motor_link.current_baud || g_motor_link.pclk1_hz == 0u)
        return;
    uint32_t brr = (g_motor_link.pclk1_hz + (baud / 2u)) / baud;
    if (brr == 0u)
        return;
    uart_set_baud(UART2_BASE, brr);
    g_motor_link.current_baud = baud;
#else
    g_motor_link.current_baud = baud;
#endif
}

static uint32_t baud_for_proto(motor_proto_t proto)
{
    return (proto == MOTOR_PROTO_V2_FIXED) ? MOTOR_BAUD_V2 : MOTOR_BAUD_DEFAULT;
}

static motor_proto_t forced_proto_for_mode(motor_link_mode_t mode)
{
    switch (mode)
    {
        case MOTOR_LINK_MODE_FORCE_SHENGYI: return MOTOR_PROTO_SHENGYI_3A1A;
        case MOTOR_LINK_MODE_FORCE_STX02:   return MOTOR_PROTO_STX02_XOR;
        case MOTOR_LINK_MODE_FORCE_AUTH:    return MOTOR_PROTO_AUTH_XOR_CR;
        case MOTOR_LINK_MODE_FORCE_V2:      return MOTOR_PROTO_V2_FIXED;
        case MOTOR_LINK_MODE_AUTO:
        default:
            return MOTOR_PROTO_SHENGYI_3A1A;
    }
}

motor_link_mode_t motor_link_get_mode(void)
{
    return g_motor_link.mode;
}

bool motor_link_is_locked(void)
{
    return g_motor_link.locked != 0u;
}

motor_proto_t motor_link_get_active_proto(void)
{
    if (g_motor_link.mode != MOTOR_LINK_MODE_AUTO)
        return forced_proto_for_mode(g_motor_link.mode);
    if (g_motor_link.locked)
        return g_motor_link.locked_proto;
    return MOTOR_PROTO_SHENGYI_3A1A;
}

static bool motor_link_send_slot_due(uint32_t now_ms, uint32_t *last_ms, uint32_t interval_ms)
{
    if (!last_ms)
        return false;

    if ((uint32_t)(now_ms - *last_ms) < interval_ms)
        return false;
    if (motor_isr_tx_busy())
        return false;

    *last_ms = now_ms;
    return true;
}

static void motor_link_reset_mode_state(void)
{
    g_motor_link.locked = 0u;
    g_motor_link.locked_proto = MOTOR_PROTO_SHENGYI_3A1A;
    memset(g_motor_link.proto_score, 0, sizeof(g_motor_link.proto_score));
    g_motor_link.last_seq_seen = 0xFFu;
    g_motor_link.probe_step = 0u;
    g_motor_link.last_probe_ms = 0u;
    g_motor_link.last_stx02_ms = 0u;
    g_motor_link.last_auth_ms = 0u;
    g_motor_link.last_v2_ms = 0u;
    g_motor_link.v2_step = 0u;
    g_motor_link.auth_phase = 0u;

    /* Reset STX02 state to OEM defaults. */
    g_motor_link.stx02_bit6_src = 0u;
    g_motor_link.stx02_bit3_src = 1u;
    g_motor_link.stx02_speed_gate = 0u;
    g_motor_link.stx02_pulse_req = 0u;
    g_motor_link.stx02_last_walk_active = 0u;
    g_motor_link.stx02_speed_filt_kph_x10 = 0u;
}

static uint16_t motor_link_effective_wheel_mm(void)
{
    return g_config_active.wheel_mm ? g_config_active.wheel_mm : MOTOR_LINK_DEFAULT_WHEEL_MM;
}

void motor_link_set_mode(motor_link_mode_t mode)
{
    g_motor_link.mode = mode;
    motor_link_reset_mode_state();

    /* OEM v2.3.0: V2 protocol uses 1200 baud, all others use 9600.
     * Switch UART2 baud rate when a forced mode is selected. */
    if (mode != MOTOR_LINK_MODE_AUTO)
    {
        motor_proto_t proto = forced_proto_for_mode(mode);
        motor_link_set_baud(baud_for_proto(proto));
    }
    else
    {
        /* AUTO mode: start at 9600 (Shengyi default). */
        motor_link_set_baud(MOTOR_BAUD_DEFAULT);
    }
}

void motor_link_init(void)
{
    memset(&g_motor_link, 0, sizeof(g_motor_link));
#ifndef HOST_TEST
    g_motor_link.pclk1_hz = rcc_get_pclk_hz_fallback(0u);
#else
    g_motor_link.pclk1_hz = 60000000u; /* 60 MHz for tests */
#endif
    motor_link_set_mode(MOTOR_LINK_MODE_AUTO);
    stx02_refresh_opts_from_config();
}

static uint8_t stx02_xor8(const uint8_t *p, uint8_t n)
{
    uint8_t x = 0u;
    for (uint8_t i = 0; i < n; ++i)
        x ^= p[i];
    return x;
}

static uint16_t wheel_diam_in_x10_from_wheel_mm(uint16_t wheel_mm)
{
    if (wheel_mm < 200u)
        return 260u;
    /* diameter_in_x10 ~= wheel_mm / (pi*25.4) * 10 ~= wheel_mm * 125 / 1000 */
    uint32_t diam = ((uint32_t)wheel_mm * 125u + 500u) / 1000u;
    if (diam < 100u) diam = 100u;
    if (diam > 600u) diam = 600u;
    return (uint16_t)diam;
}

static uint8_t stx02_profile_type_from_gear_count(uint8_t gears)
{
    /* OEM uses 3/5/9. Keep closest >= 1. */
    if (gears <= 3u) return 3u;
    if (gears <= 5u) return 5u;
    return 9u;
}

static uint8_t stx02_power_level_from_gear_oem(uint8_t gear, uint8_t gears_total)
{
    /*
     * OEM v2.5.1 mapping (APP_process_motor_control_flags @ 0x80222D4):
     * - max_gears == 3: n10 = {0,5,10,15}
     * - max_gears == 5: n10 = {0,3,6,9,12,15}
     * - max_gears == 9: n10 = {0,1,3,5,7,9,11,13,14,15}
     *
     * The OEM also has "special" states that keep the previous n10; we don't
     * reproduce that here since open-firmware's assist selection is explicit.
     */
    if (gear == 0u)
        return 0u;

    if (gears_total != 3u && gears_total != 5u && gears_total != 9u)
        gears_total = stx02_profile_type_from_gear_count(gears_total);
    if (gear > gears_total)
        gear = gears_total;

    if (gears_total == 3u)
        return (uint8_t)(gear * 5u);
    if (gears_total == 5u)
        return (uint8_t)(gear * 3u);

    /* gears_total == 9 */
    static const uint8_t map9[10] = {0u, 1u, 3u, 5u, 7u, 9u, 11u, 13u, 14u, 15u};
    if (gear < (uint8_t)(sizeof(map9) / sizeof(map9[0])))
        return map9[gear];
    return 15u;
}

/* OEM "non-0x3A" status packet (19 bytes + XOR).
 * docs/firmware/README.md describes the payload layout for v2.5.1-style builds. */
static uint8_t stx02_build_status_0x14(uint8_t *out, uint8_t cap)
{
    if (!out || cap < MOTOR_LINK_STX02_FRAME_LEN)
        return 0u;

    /* Keep STX02 option bits sourced from persistent config. */
    stx02_refresh_opts_from_config();

    uint16_t wheel_mm = motor_link_effective_wheel_mm();
    uint16_t diam_x10 = wheel_diam_in_x10_from_wheel_mm(wheel_mm);

    uint16_t kph_x10 = dmph_to_kph_x10(g_effective_cap_speed_dmph);
    if (kph_x10 > MOTOR_LINK_STX02_FRAME_LIMIT_KPH_X10)
        kph_x10 = MOTOR_LINK_STX02_FRAME_LIMIT_KPH_X10;
    uint8_t speed_limit_kph = (uint8_t)(kph_x10 / 10u);
    uint16_t speed_limit_kph_x10 = (uint16_t)speed_limit_kph * 10u;

    uint16_t cap_current_dA = g_effective_cap_current_dA;
    uint8_t current_limit_A = (uint8_t)((cap_current_dA + 5u) / 10u);

    uint8_t gears_total = g_vgears.count ? g_vgears.count : 3u;
    uint8_t gears_oem = stx02_profile_type_from_gear_count(gears_total);
    uint8_t power_level = stx02_power_level_from_gear_oem(g_outputs.virtual_gear, gears_oem);

    /*
     * OEM v2.5.1 flag sources (APP_process_motor_control_flags @ 0x80222D4):
     * - bit7: always 1
     * - bit6: byte_20001E55 & 1 (default 0)
     * - bit5: byte_20001DA9 & 1 (user-toggled flag)
     * - bit3: byte_20001E56 & 1 (default 1)
     * - bit2: OEM toggles this when filtered speed (kph*10) exceeds speed limit (kph*10),
     *   gated by byte_20001E65. Semantics: likely speed-limit indicator/enforcement flag.
     * - bit1: byte_20001DA6 & 1 (special mode request; likely walk/cruise)
     * - bit0: one-shot pulse (byte_20001DA4), only used when byte_20001E56==0
     *
     * We don't fully model all OEM internal variables yet, so we implement:
     * - stable OEM-ish defaults: bit7 set, bit3 set (via stx02_bit3_src), bit6 clear
     * - user-facing toggles we do have: headlight, walk
     */
    /* Track walk edge to generate an OEM-like one-shot pulse request. */
    uint8_t walk_active = bool_to_u8(g_walk_state == WALK_STATE_ACTIVE);
    if (walk_active && !g_motor_link.stx02_last_walk_active)
        g_motor_link.stx02_pulse_req = 1u;
    g_motor_link.stx02_last_walk_active = walk_active;

    uint8_t flags = 0x80u; /* bit7 */
    if (g_motor_link.stx02_bit6_src)
        flags |= (1u << 6);
    if (g_headlight_enabled)
        flags |= (1u << 5);
    if (g_motor_link.stx02_bit3_src)
        flags |= (1u << 3);

    /* bit2: OEM speed-limit flag (gated by byte_20001E65). Default off unless enabled. */
    uint16_t cur_kph_x10 = dmph_to_kph_x10(g_inputs.speed_dmph);
    uint16_t filt_kph_x10 = stx02_speed_filter_update(cur_kph_x10);
    if (g_motor_link.stx02_speed_gate && (filt_kph_x10 > speed_limit_kph_x10))
        flags |= (1u << 2);

    if (walk_active)
        flags |= (1u << 1);

    /* bit0: one-shot pulse, only when bit3_src is disabled (matches OEM v2.5.1 behavior). */
    if (!g_motor_link.stx02_bit3_src && g_motor_link.stx02_pulse_req)
    {
        flags |= 1u;
        g_motor_link.stx02_pulse_req = 0u;
    }

    out[0] = 0x01u; /* frame type */
    out[1] = MOTOR_LINK_STX02_FRAME_LEN; /* length (data+checksum) */
    out[2] = 0x01u; /* frame counter (OEM is constant) */
    /*
     * OEM byte_20001E54 default is 2 (see sub_801AB64). It is not the 3/5/9
     * assist count; keep a stable default value for compatibility.
     */
    out[3] = 0x02u;
    out[4] = power_level;
    out[5] = flags;
    out[6] = 0x01u; /* display setting (OEM default is 1) */
    out[7] = (uint8_t)(diam_x10 >> 8);
    out[8] = (uint8_t)(diam_x10 & 0xFFu);
    /* OEM v2.5.1 uses 3 config-derived bytes here (see `sub_801AB64` defaults and
     * `sub_802164C` config update path 0xC0): n3_1, n3_2, byte_20001E5B.
     * We do not model these yet; keep OEM defaults (3,3,0) for compatibility. */
    out[9] = 3u;
    out[10] = 3u;
    out[11] = 0u;
    out[12] = speed_limit_kph;
    out[13] = current_limit_A;
    /* OEM default is 42000mV -> 420 (mV/100). Keep a stable nonzero threshold. */
    uint16_t batt_thr_q = (uint16_t)((MOTOR_LINK_STX02_BATT_THRESHOLD_MV + 50u) / 100u);
    out[14] = (uint8_t)(batt_thr_q >> 8);
    out[15] = (uint8_t)(batt_thr_q & 0xFFu);
    out[16] = 0u;
    out[17] = 0u;
    out[18] = (uint8_t)(g_motor.err & 0x0Fu);
    out[MOTOR_LINK_STX02_FRAME_LEN - 1u] = stx02_xor8(out, (uint8_t)(MOTOR_LINK_STX02_FRAME_LEN - 1u));
    return MOTOR_LINK_STX02_FRAME_LEN;
}

static void stx02_refresh_opts_from_config(void)
{
    /* reserved==0 maps to OEM defaults: bit6=0, bit3=1, speed_gate=0 */
    uint16_t r = g_config_active.reserved;
    g_motor_link.stx02_bit6_src = bool_to_u8(r & CFG_RSVD_STX02_BIT6_ENABLE);
    g_motor_link.stx02_bit3_src = bool_to_u8((r & CFG_RSVD_STX02_BIT3_DISABLE) == 0u);
    g_motor_link.stx02_speed_gate = bool_to_u8(r & CFG_RSVD_STX02_SPEED_GATE_ENABLE);
}

static uint16_t dmph_to_kph_x10(uint16_t dmph)
{
    /* kph_x10 ~= dmph * 1.60934 */
    uint32_t k = ((uint32_t)dmph * 1609u + 500u) / 1000u;
    if (k > 0xFFFFu) k = 0xFFFFu;
    return (uint16_t)k;
}

static uint16_t stx02_speed_filter_update(uint16_t target_kph_x10)
{
    /* OEM v2.5.1 filter (`sub_8021574`):
     * - filtered ramps toward target by a step = abs(delta)/5
     * - if target==0 and step==0 in the "decreasing" case, force filtered=0 */
    uint16_t filt = g_motor_link.stx02_speed_filt_kph_x10;
    if (target_kph_x10 <= filt)
    {
        uint16_t diff = (uint16_t)(filt - target_kph_x10);
        uint16_t step = (uint16_t)(diff / 5u);
        if (step == 0u && target_kph_x10 == 0u)
            filt = 0u;
        else if (filt < step)
            filt = 0u;
        else
            filt = (uint16_t)(filt - step);
    }
    else
    {
        uint16_t diff = (uint16_t)(target_kph_x10 - filt);
        uint16_t step = (uint16_t)(diff / 5u);
        uint32_t next = (uint32_t)filt + (uint32_t)step;
        if (next >= target_kph_x10)
            filt = target_kph_x10;
        else
            filt = (uint16_t)next;
    }

    g_motor_link.stx02_speed_filt_kph_x10 = filt;
    return filt;
}

static uint8_t wheel_code_from_wheel_mm(uint16_t wheel_mm)
{
    /* Convert circumference(mm) to diameter(in)*10 ~= wheel_mm / (pi*2.54) */
    if (wheel_mm < 200u)
        return 5u;

    /* diameter_in_x10 = wheel_mm / (pi*25.4) * 10
     * approximate: 10/(pi*25.4) ~= 0.1253 => wheel_mm * 125 / 1000 */
    uint32_t diam_in_x10 = ((uint32_t)wheel_mm * 125u + 500u) / 1000u;

    /* OEM mapping table values (inches*10): 160, 180, 200, 220, 240, 260, 275, 290 */
    static const uint16_t codes[] = {160u, 180u, 200u, 220u, 240u, 260u, 275u, 290u};
    uint8_t best = 5u;
    uint32_t best_err = 0xFFFFFFFFu;
    for (uint8_t i = 0u; i < (uint8_t)(sizeof(codes) / sizeof(codes[0])); ++i)
    {
        uint32_t c = codes[i];
        uint32_t err = (diam_in_x10 > c) ? (diam_in_x10 - c) : (c - diam_in_x10);
        if (err < best_err)
        {
            best_err = err;
            best = i;
        }
    }
    return best;
}

static uint8_t auth_build_frame(uint8_t sof, uint8_t b1, uint8_t b2, uint8_t *out, uint8_t cap)
{
    if (!out || cap < 6u)
        return 0u;

    /* OEM v2.5.1 (`finalize_auth_packet_sram_buffers` @ 0x8023E54):
     * appends a single extra byte (0..255) and requires XOR(payload) != 0x0D
     * because 0x0D is the terminator. XOR excludes byte0 (SOF). */
    uint8_t nonce = 0u;
    uint8_t x = 0u;
    for (uint16_t i = 0; i < 256u; ++i)
    {
        nonce = (uint8_t)i;
        x = (uint8_t)(b1 ^ b2 ^ nonce);
        if (x != 0x0Du)
            break;
    }

    out[0] = sof;
    out[1] = b1;
    out[2] = b2;
    out[3] = nonce;
    out[4] = x;
    out[5] = 0x0Du;
    return 6u;
}

static void auth_compute_bytes(uint8_t *out_b1, uint8_t *out_b2)
{
    if (!out_b1 || !out_b2)
        return;

    /* OEM v2.5.1 (`sub_8023CA4` @ 0x8023CA4):
     * - bits0..3: assist nibble, with 0 encoded as 0xF
     * - bit4: special request (source is byte_20001DA6)
     * - bit7: light enabled (source is byte_20001DA9)
     * - bits5..6 are left as-is in OEM; open-firmware clears them. */
    uint8_t assist = (g_outputs.virtual_gear > 15u) ? 15u : g_outputs.virtual_gear;
    if (assist == 0u)
        assist = 15u;

    uint8_t b1 = (assist & 0x0Fu);
    if (g_walk_state == WALK_STATE_ACTIVE)
        b1 |= (1u << 4);
    if (g_headlight_enabled)
        b1 |= (1u << 7);

    /* Speed limit encoding: clamp to <= 51.0 km/h (0x1FE). */
    uint16_t dmph = g_effective_cap_speed_dmph;
    uint16_t kph_x10 = dmph_to_kph_x10(dmph);
    if (kph_x10 > MOTOR_LINK_STX02_FRAME_LIMIT_KPH_X10)
        kph_x10 = MOTOR_LINK_STX02_FRAME_LIMIT_KPH_X10;
    uint16_t kph = (uint16_t)(kph_x10 / 10u);
    int32_t field = (int32_t)kph - 20;
    if (field < 0) field = 0;
    if (field > 31) field = 31;

    uint8_t wheel_code = wheel_code_from_wheel_mm(motor_link_effective_wheel_mm());
    uint8_t b2 = (uint8_t)(((uint8_t)field & 0x1Fu) << 3) | (wheel_code & 0x07u);

    *out_b1 = b1;
    *out_b2 = b2;
}

static void motor_link_observe_rx(void)
{
    uint8_t frame[SHENGYI_MAX_FRAME_SIZE];
    uint8_t len = 0u, op = 0u, seq = 0u;
    motor_proto_t proto = MOTOR_PROTO_SHENGYI_3A1A;
    uint16_t aux16 = 0u;
    if (!motor_isr_copy_last_frame(frame, (uint8_t)sizeof(frame), &len, &op, &seq, &proto, &aux16))
        return;

    if (seq == g_motor_link.last_seq_seen)
        return;
    g_motor_link.last_seq_seen = seq;

    if ((uint8_t)proto < MOTOR_LINK_PROTO_SLOT_COUNT)
    {
        if (g_motor_link.proto_score[(uint8_t)proto] < 250u)
            g_motor_link.proto_score[(uint8_t)proto]++;

        if (g_motor_link.mode == MOTOR_LINK_MODE_AUTO && !g_motor_link.locked)
        {
            /* Conservative: require two frames of the same type. */
            if (g_motor_link.proto_score[(uint8_t)proto] >= 2u)
            {
                g_motor_link.locked = 1u;
                g_motor_link.locked_proto = proto;
                g_motor_link.v2_step = 0u;
                g_motor_link.auth_phase = 0u;
                motor_link_set_baud(baud_for_proto(proto));
            }
        }
    }
}

static void motor_link_probe_tick(void)
{
    if (!motor_link_send_slot_due(g_ms, &g_motor_link.last_probe_ms, PROBE_INTERVAL_MS))
        return;

    switch (g_motor_link.probe_step & 3u)
    {
        case 0u:
            /* Probe Shengyi: a minimal 0x52 request. */
            (void)motor_isr_queue_cmd(0u, false, false, false, false);
            break;
        case 1u:
        {
            /* Probe STX02: OEM-style 0x14 status packet (display->controller). */
            uint8_t pkt[32];
            uint8_t n = stx02_build_status_0x14(pkt, (uint8_t)sizeof(pkt));
            if (n)
                (void)motor_isr_queue_frame(pkt, n);
            break;
        }
        case 2u:
        {
            /* Probe AUTH: send a basic 'F' frame. */
            uint8_t b1 = 0u, b2 = 0u;
            auth_compute_bytes(&b1, &b2);
            uint8_t pkt[8];
            uint8_t n = auth_build_frame(0x46u, b1, b2, pkt, (uint8_t)sizeof(pkt));
            if (n)
                (void)motor_isr_queue_frame(pkt, n);
            break;
        }
        case 3u:
        default:
        {
            /* Probe v2: request 0x11 0x90, expect 5-byte response. */
            uint8_t req[2] = { 0x11u, 0x90u };
            motor_isr_v2_expect(0x1190u, 5u);
            (void)motor_isr_queue_frame(req, (uint8_t)sizeof(req));
            break;
        }
    }

    g_motor_link.probe_step++;
}

static void motor_link_stx02_tick(void)
{
    if (!motor_link_send_slot_due(g_ms, &g_motor_link.last_stx02_ms, STX02_INTERVAL_MS))
        return;

    uint8_t pkt[32];
    uint8_t n = stx02_build_status_0x14(pkt, (uint8_t)sizeof(pkt));
    if (n)
        (void)motor_isr_queue_frame(pkt, n);
}

static uint16_t v2_speed_limit_code(void)
{
    /* OEM uses 0x1FE as a common constant; use effective cap if present. */
    uint16_t dmph = g_config_active.cap_speed_dmph;
    if (dmph == 0u)
        return MOTOR_LINK_V2_SPEED_LIMIT_CODE;
    uint16_t kph_x10 = dmph_to_kph_x10(dmph);
    if (kph_x10 > MOTOR_LINK_V2_SPEED_LIMIT_CODE)
        kph_x10 = MOTOR_LINK_V2_SPEED_LIMIT_CODE;
    return kph_x10;
}

static void motor_link_v2_send_req_u16(uint16_t msg_id, uint8_t expected_total, uint8_t b0, uint8_t b1)
{
    uint8_t req[2] = { b0, b1 };
    if (expected_total)
        motor_isr_v2_expect(msg_id, expected_total);
    (void)motor_isr_queue_frame(req, (uint8_t)sizeof(req));
}

static void motor_link_v2_send_req_161f(uint16_t code)
{
    uint8_t hi = (uint8_t)(code >> 8);
    uint8_t lo = (uint8_t)(code & 0xFFu);
    uint8_t cks = (uint8_t)(0x35u + hi + lo);
    uint8_t req[5] = { 0x16u, 0x1Fu, hi, lo, cks };
    (void)motor_isr_queue_frame(req, (uint8_t)sizeof(req));
}

static void motor_link_v2_tick(void)
{
    if (!motor_link_send_slot_due(g_ms, &g_motor_link.last_v2_ms, V2_INTERVAL_MS))
        return;

    switch (g_motor_link.v2_step)
    {
        case 0u:
            motor_link_v2_send_req_u16(0x1190u, 5u, 0x11u, 0x90u);
            g_motor_link.v2_step++;
            break;
        case 1u:
            motor_link_v2_send_req_161f(v2_speed_limit_code());
            g_motor_link.v2_step++;
            break;
        case 2u:
            /* OEM increments state without sending; keep timing similar. */
            g_motor_link.v2_step++;
            break;
        case 3u:
            motor_link_v2_send_req_u16(0x1120u, 5u, 0x11u, 0x20u);
            g_motor_link.v2_step++;
            break;
        case 4u:
            motor_link_v2_send_req_u16(0x1108u, 3u, 0x11u, 0x08u);
            g_motor_link.v2_step++;
            break;
        case 5u:
            motor_link_v2_send_req_u16(0x1111u, 4u, 0x11u, 0x11u);
            g_motor_link.v2_step++;
            break;
        case 6u:
            motor_link_v2_send_req_u16(0x1131u, 4u, 0x11u, 0x31u);
            g_motor_link.v2_step++;
            break;
        case 7u:
        default:
            motor_link_v2_send_req_u16(0x110Au, 4u, 0x11u, 0x0Au);
            g_motor_link.v2_step = 1u;
            break;
    }
}

static void motor_link_auth_tick(void)
{
    if (!motor_link_send_slot_due(g_ms, &g_motor_link.last_auth_ms, AUTH_INTERVAL_MS))
        return;

    uint8_t b1 = 0u, b2 = 0u;
    auth_compute_bytes(&b1, &b2);

    uint8_t pkt[8];
    uint8_t sof = (g_motor_link.auth_phase & 1u) ? 0x53u : 0x46u;
    uint8_t n = auth_build_frame(sof, b1, b2, pkt, (uint8_t)sizeof(pkt));
    if (n)
        (void)motor_isr_queue_frame(pkt, n);
    g_motor_link.auth_phase++;
}

void motor_link_switch_protocol(uint8_t proto_idx)
{
    /*
     * OEM v2.3.0 (shengyi_rx_frame_dispatch, opcode 0xAB):
     * Motor controller sends 0xAB with payload[1] = new protocol index.
     * Display reinitializes as if that protocol was selected at boot.
     */
    motor_link_mode_t mode;
    switch (proto_idx)
    {
        case 0u: mode = MOTOR_LINK_MODE_FORCE_SHENGYI; break;
        case 1u: mode = MOTOR_LINK_MODE_FORCE_STX02;   break;
        case 2u: mode = MOTOR_LINK_MODE_FORCE_V2;      break;
        case 3u: mode = MOTOR_LINK_MODE_FORCE_AUTH;     break;
        default: return; /* unknown index */
    }
    motor_link_set_mode(mode);
    /* Re-init Shengyi module (OEM always calls proto0_shengyi_init on switch). */
    shengyi_init();
}

void motor_link_periodic_send_tick(void)
{
    motor_link_observe_rx();

    if (g_motor_link.mode == MOTOR_LINK_MODE_AUTO && !g_motor_link.locked)
    {
        motor_link_probe_tick();
        return;
    }

    motor_proto_t active = motor_link_get_active_proto();
    switch (active)
    {
        case MOTOR_PROTO_SHENGYI_3A1A:
            shengyi_periodic_send_tick();
            break;
        case MOTOR_PROTO_STX02_XOR:
            motor_link_stx02_tick();
            break;
        case MOTOR_PROTO_AUTH_XOR_CR:
            motor_link_auth_tick();
            break;
        case MOTOR_PROTO_V2_FIXED:
            motor_link_v2_tick();
            break;
        default:
            break;
    }
}
