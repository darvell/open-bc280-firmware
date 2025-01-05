#include "config.h"

#include <stddef.h>

#include "app_data.h"
#include "control/control.h"
#include "drivers/spi_flash.h"
#include "input/input.h"
#include "power/power.h"
#include "app_state.h"
#include "src/profiles/profiles.h"
#include "storage/layout.h"
#include "storage/logs.h"
#include "ui.h"
#include "util/byteorder.h"
#include "util/crc32.h"
#include "platform/time.h"

#define PIN_ATTEMPT_FLAG_OK    0x01u
#define PIN_ATTEMPT_FLAG_BAD   0x02u
#define PIN_ATTEMPT_FLAG_RATE  0x04u

#define OEM_CFG_PRIMARY_ADDR 0x003FD000u
#define OEM_CFG_BACKUP_ADDR  0x003FB000u
#define OEM_CFG_SIZE         0xD0u

#define OEM_CFG_OFF_WHEEL_MM     0x1Cu
#define OEM_CFG_OFF_SPEED_LIMIT  0x7Cu

config_t g_config_active;
static config_t g_config_staged;
static uint8_t g_config_staged_valid;
static int g_config_active_slot;
static uint32_t g_pin_last_attempt_ms;

static void config_apply_active(const config_t *c, int slot)
{
    if (!c)
        return;
    g_config_active = *c;
    g_config_active_slot = slot;
    if (g_config_active.profile_id >= PROFILE_COUNT)
        g_config_active.profile_id = 0;
    set_active_profile(g_config_active.profile_id, 0);
    g_stream_log_period_ms = stream_log_period_sanitize(g_config_active.log_period_ms);
    drive_apply_config();
    if (g_stream_log_enabled)
    {
        g_stream_log_last_ms = g_ms;
        g_stream_log_last_sample_ms = 0;
    }
}

static uint16_t load_le16(const uint8_t *p)
{
    return (uint16_t)p[0] | (uint16_t)((uint16_t)p[1] << 8);
}

static int oem_blob_valid(const uint8_t *buf)
{
    uint8_t all_zero = 1u;
    uint8_t all_ff = 1u;
    for (uint32_t i = 0; i < OEM_CFG_SIZE; ++i)
    {
        if (buf[i] != 0u)
            all_zero = 0u;
        if (buf[i] != 0xFFu)
            all_ff = 0u;
    }
    return (all_zero || all_ff) ? 0 : 1;
}

static int config_try_import_oem(config_t *c)
{
    if (!c)
        return 0;

    uint8_t buf[OEM_CFG_SIZE];
    spi_flash_read(OEM_CFG_PRIMARY_ADDR, buf, OEM_CFG_SIZE);
    if (!oem_blob_valid(buf))
    {
        spi_flash_read(OEM_CFG_BACKUP_ADDR, buf, OEM_CFG_SIZE);
        if (!oem_blob_valid(buf))
            return 0;
    }

    uint16_t wheel_mm = load_le16(&buf[OEM_CFG_OFF_WHEEL_MM]);
    if (wheel_mm >= 1000u && wheel_mm <= 4000u)
        c->wheel_mm = wheel_mm;

    uint16_t speed_limit = load_le16(&buf[OEM_CFG_OFF_SPEED_LIMIT]);
    if (speed_limit <= 3000u)
        c->cap_speed_dmph = speed_limit;

    c->crc32 = 0;
    c->crc32 = config_crc_expected(c);
    return 1;
}

void config_defaults(config_t *c)
{
    if (!c)
        return;
    c->version = CONFIG_VERSION;
    c->size = CONFIG_BLOB_SIZE;
    c->reserved = 0;
    c->seq = 1;
    c->wheel_mm = 2100; /* common 700c wheel */
    c->units = 0;       /* imperial default */
    c->profile_id = 0;
    c->theme = UI_THEME_NIGHT;
    c->flags = CAP_FLAG_WALK; /* enable walk capability by default */
    c->button_map = 0;
    c->button_flags = 0;
    c->mode = MODE_STREET;
    c->pin_code = MODE_PIN_DEFAULT;
    c->cap_current_dA = STREET_MAX_CURRENT_DA;  /* street cap by default */
    c->cap_speed_dmph = STREET_MAX_SPEED_DMPH;  /* street cap by default */
    c->log_period_ms = 1000;  /* 1s logging/streaming hint */
    c->soft_start_ramp_wps = SOFT_START_RAMP_DEFAULT_WPS;
    c->soft_start_deadband_w = SOFT_START_DEADBAND_DEFAULT_W;
    c->soft_start_kick_w = SOFT_START_KICK_DEFAULT_W;
    c->drive_mode = DRIVE_MODE_AUTO;
    c->manual_current_dA = 180u;
    c->manual_power_w = 400u;
    c->boost_budget_ms = BOOST_BUDGET_DEFAULT_MS;
    c->boost_cooldown_ms = BOOST_COOLDOWN_DEFAULT_MS;
    c->boost_threshold_dA = BOOST_THRESHOLD_DEFAULT_DA;
    c->boost_gain_q15 = BOOST_GAIN_DEFAULT_Q15;
    c->curve_count = 0;
    for (uint8_t i = 0; i < ASSIST_CURVE_MAX_POINTS; ++i)
    {
        c->curve[i].x = 0;
        c->curve[i].y = 0;
    }
    c->crc32 = 0;
    c->crc32 = config_crc_expected(c);
}

void config_store_be(uint8_t *dst, const config_t *c)
{
    if (!dst || !c)
        return;
    dst[0] = c->version;
    dst[1] = c->size;
    store_be16(&dst[2], c->reserved);
    store_be32(&dst[4], c->seq);
    store_be32(&dst[8], c->crc32);
    store_be16(&dst[12], c->wheel_mm);
    dst[14] = c->units;
    dst[15] = c->profile_id;
    dst[16] = c->theme;
    dst[17] = c->flags;
    dst[18] = c->button_map;
    dst[19] = c->button_flags;
    dst[20] = c->mode;
    store_be16(&dst[21], c->pin_code);
    store_be16(&dst[23], c->cap_current_dA);
    store_be16(&dst[25], c->cap_speed_dmph);
    store_be16(&dst[27], c->log_period_ms);
    store_be16(&dst[29], c->soft_start_ramp_wps);
    store_be16(&dst[31], c->soft_start_deadband_w);
    store_be16(&dst[33], c->soft_start_kick_w);
    dst[35] = c->drive_mode;
    store_be16(&dst[36], c->manual_current_dA);
    store_be16(&dst[38], c->manual_power_w);
    store_be16(&dst[40], c->boost_budget_ms);
    store_be16(&dst[42], c->boost_cooldown_ms);
    store_be16(&dst[44], c->boost_threshold_dA);
    store_be16(&dst[46], c->boost_gain_q15);
    dst[CONFIG_BLOB_CURVE_COUNT_OFFSET] = c->curve_count;
    for (uint8_t i = 0; i < ASSIST_CURVE_MAX_POINTS; ++i)
    {
        size_t off = CONFIG_BLOB_CURVE_OFFSET + (size_t)i * 4u;
        store_be16(&dst[off], c->curve[i].x);
        store_be16(&dst[off + 2u], c->curve[i].y);
    }
}

void config_load_from_be(config_t *c, const uint8_t *src)
{
    if (!c || !src)
        return;
    c->version    = src[0];
    c->size       = src[1];
    c->reserved   = load_be16(&src[2]);
    c->seq        = load_be32(&src[4]);
    c->crc32      = load_be32(&src[8]);
    c->wheel_mm   = load_be16(&src[12]);
    c->units      = src[14];
    c->profile_id = src[15];
    c->theme      = src[16];
    c->flags      = src[17];
    c->button_map = src[18];
    c->button_flags = src[19];
    c->mode       = src[20];
    c->pin_code   = load_be16(&src[21]);
    c->cap_current_dA = load_be16(&src[23]);
    c->cap_speed_dmph = load_be16(&src[25]);
    c->log_period_ms  = load_be16(&src[27]);
    c->soft_start_ramp_wps = load_be16(&src[29]);
    c->soft_start_deadband_w = load_be16(&src[31]);
    c->soft_start_kick_w = load_be16(&src[33]);
    c->drive_mode = src[35];
    c->manual_current_dA = load_be16(&src[36]);
    c->manual_power_w = load_be16(&src[38]);
    c->boost_budget_ms = load_be16(&src[40]);
    c->boost_cooldown_ms = load_be16(&src[42]);
    c->boost_threshold_dA = load_be16(&src[44]);
    c->boost_gain_q15 = load_be16(&src[46]);
    c->curve_count    = src[CONFIG_BLOB_CURVE_COUNT_OFFSET];
    for (uint8_t i = 0; i < ASSIST_CURVE_MAX_POINTS; ++i)
    {
        size_t off = CONFIG_BLOB_CURVE_OFFSET + (size_t)i * 4u;
        c->curve[i].x = load_be16(&src[off]);
        c->curve[i].y = load_be16(&src[off + 2u]);
    }
}

uint32_t config_crc_expected(const config_t *c)
{
    if (!c)
        return 0;
    config_t tmp = *c;
    tmp.crc32 = 0;
    uint8_t buf[CONFIG_BLOB_SIZE];
    config_store_be(buf, &tmp);
    return crc32_compute(buf, CONFIG_BLOB_SIZE);
}

int config_validate_reason(const config_t *c, int check_crc, config_reject_reason_t *reason_out)
{
    config_reject_reason_t r = CFG_REJECT_NONE;
    if (!c)
        goto fail;
    if (c->version != CONFIG_VERSION || c->size != CONFIG_BLOB_SIZE)
    {
        r = CFG_REJECT_UNSUPPORTED;
        goto fail;
    }
    if (c->wheel_mm < 500 || c->wheel_mm > 5000 ||
        c->units > 1 ||
        c->profile_id >= PROFILE_COUNT ||
        c->theme > 7 ||
        c->button_map > BUTTON_MAP_MAX ||
        (c->button_flags & (uint8_t)~BUTTON_FLAGS_ALLOWED) != 0 ||
        c->mode > MODE_PRIVATE ||
        c->pin_code > MODE_PIN_MAX ||
        c->cap_current_dA < 50 || c->cap_current_dA > 300 ||
        c->cap_speed_dmph > 800 ||
        c->log_period_ms < STREAM_LOG_PERIOD_MIN_MS || c->log_period_ms > STREAM_LOG_PERIOD_MAX_MS ||
        (c->soft_start_ramp_wps != 0 &&
         (c->soft_start_ramp_wps < SOFT_START_RAMP_MIN_WPS ||
          c->soft_start_ramp_wps > SOFT_START_RAMP_MAX_WPS)) ||
        c->soft_start_deadband_w > SOFT_START_DEADBAND_MAX_W ||
        c->soft_start_kick_w > SOFT_START_KICK_MAX_W ||
        c->drive_mode > DRIVE_MODE_SPORT ||
        c->manual_current_dA > MANUAL_CURRENT_MAX_DA ||
        c->manual_power_w > MANUAL_POWER_MAX_W ||
        c->boost_budget_ms > BOOST_BUDGET_MAX_MS ||
        c->boost_cooldown_ms > BOOST_COOLDOWN_MAX_MS ||
        c->boost_threshold_dA > MANUAL_CURRENT_MAX_DA ||
        c->boost_gain_q15 > 65535u)
    {
        r = CFG_REJECT_RANGE;
        goto fail;
    }
    if (c->curve_count > ASSIST_CURVE_MAX_POINTS)
    {
        r = CFG_REJECT_RANGE;
        goto fail;
    }
    if (c->curve_count > 0)
    {
        uint16_t prev_x = 0;
        for (uint8_t i = 0; i < c->curve_count; ++i)
        {
            uint16_t x = c->curve[i].x;
            uint16_t y = c->curve[i].y;
            if (i == 0)
                prev_x = x;
            else
            {
                if (x <= prev_x)
                {
                    r = CFG_REJECT_MONOTONIC;
                    goto fail;
                }
                prev_x = x;
            }
            if (x > 400 || y > 1200)
            {
                r = CFG_REJECT_RANGE;
                goto fail;
            }
        }
    }
    if (c->flags & (uint8_t)~(CAP_FLAG_WALK | CAP_FLAG_REGEN |
                             CFG_FLAG_QA_CRUISE | CFG_FLAG_QA_PROFILE | CFG_FLAG_QA_CAPTURE |
                             CFG_FLAG_ADAPT_EFFORT | CFG_FLAG_ADAPT_ECO | CFG_FLAG_QA_FOCUS))
    {
        r = CFG_REJECT_UNSUPPORTED;
        goto fail;
    }
    if (check_crc && c->crc32 != config_crc_expected(c))
    {
        r = CFG_REJECT_CRC;
        goto fail;
    }
    if (reason_out)
        *reason_out = CFG_REJECT_NONE;
    return 1;
fail:
    if (reason_out)
        *reason_out = r;
    return 0;
}

int config_validate(const config_t *c, int check_crc)
{
    return config_validate_reason(c, check_crc, NULL);
}

int config_policy_validate(const config_t *c, config_reject_reason_t *reason_out)
{
    config_reject_reason_t r = CFG_REJECT_NONE;
    if (!c)
        goto fail;

    if (c->mode == MODE_STREET)
    {
        if (c->cap_current_dA > STREET_MAX_CURRENT_DA ||
            c->cap_speed_dmph == 0 ||
            c->cap_speed_dmph > STREET_MAX_SPEED_DMPH)
        {
            r = CFG_REJECT_POLICY;
            goto fail;
        }

        if (g_config_active.mode == MODE_STREET && c->pin_code != g_config_active.pin_code)
        {
            r = CFG_REJECT_PIN;
            goto fail;
        }
    }

    if (g_config_active.mode == MODE_STREET && c->mode == MODE_PRIVATE)
    {
        uint8_t flags = 0;
        if (g_pin_last_attempt_ms &&
            (g_ms - g_pin_last_attempt_ms) < MODE_PIN_RATE_LIMIT_MS)
        {
            flags |= PIN_ATTEMPT_FLAG_RATE;
            event_log_append(EVT_PIN_ATTEMPT, flags);
            r = CFG_REJECT_RATE;
            goto fail;
        }
        g_pin_last_attempt_ms = g_ms;
        if (c->pin_code != g_config_active.pin_code)
        {
            flags |= PIN_ATTEMPT_FLAG_BAD;
            event_log_append(EVT_PIN_ATTEMPT, flags);
            r = CFG_REJECT_PIN;
            goto fail;
        }
        flags |= PIN_ATTEMPT_FLAG_OK;
        event_log_append(EVT_PIN_ATTEMPT, flags);
    }

    if (reason_out)
        *reason_out = CFG_REJECT_NONE;
    return 1;
fail:
    if (reason_out)
        *reason_out = r;
    return 0;
}

void config_write_slot(int slot, const config_t *c)
{
    if (!c || slot < 0 || slot >= CONFIG_SLOT_COUNT)
        return;
    uint32_t base = CONFIG_STORAGE_BASE + (uint32_t)slot * CONFIG_SLOT_STRIDE;
    uint8_t buf[CONFIG_BLOB_SIZE];
    config_store_be(buf, c);
    spi_flash_erase_4k(base);
    spi_flash_write(base, buf, CONFIG_BLOB_SIZE);
}

int config_read_slot(int slot, config_t *out)
{
    if (!out || slot < 0 || slot >= CONFIG_SLOT_COUNT)
        return 0;
    uint32_t base = CONFIG_STORAGE_BASE + (uint32_t)slot * CONFIG_SLOT_STRIDE;
    uint8_t buf[CONFIG_BLOB_SIZE];
    spi_flash_read(base, buf, CONFIG_BLOB_SIZE);
    config_load_from_be(out, buf);
    return config_validate(out, 1);
}

void config_load_active(void)
{
    config_t best = {0};
    int best_slot = -1;
    for (int i = 0; i < CONFIG_SLOT_COUNT; ++i)
    {
        config_t tmp;
        if (config_read_slot(i, &tmp))
        {
            if (best_slot < 0 || tmp.seq > best.seq)
            {
                best = tmp;
                best_slot = i;
            }
        }
    }
    if (best_slot < 0)
    {
        config_defaults(&best);
        (void)config_try_import_oem(&best);
        best_slot = 0;
        config_write_slot(best_slot, &best);
    }
    g_config_active = best;
    g_config_active_slot = best_slot;
    if (g_config_active.profile_id >= PROFILE_COUNT)
        g_config_active.profile_id = 0;
    g_active_profile_id = g_config_active.profile_id;
    g_outputs.profile_id = g_active_profile_id;
    g_stream_log_period_ms = stream_log_period_sanitize(g_config_active.log_period_ms);
    drive_apply_config();
}

void config_persist_active(void)
{
    g_config_active.seq += 1u;
    g_config_active.crc32 = 0;
    g_config_active.crc32 = config_crc_expected(&g_config_active);
    int slot = (g_config_active_slot + 1) % CONFIG_SLOT_COUNT;
    config_write_slot(slot, &g_config_active);
    g_config_active_slot = slot;
}

int config_commit_active(const config_t *c)
{
    if (!c)
        return 0;
    int slot = (g_config_active_slot + 1) % CONFIG_SLOT_COUNT;
    config_write_slot(slot, c);
    config_apply_active(c, slot);
    return 1;
}

void config_stage_reset(void)
{
    g_config_staged_valid = 0;
}

uint8_t config_stage_blob(const uint8_t *p)
{
    g_config_staged_valid = 0;
    config_t tmp;
    config_load_from_be(&tmp, p);
    /* Require CRC on incoming payload to detect corruption. */
    config_reject_reason_t reason = CFG_REJECT_NONE;
    if (!config_validate_reason(&tmp, 1, &reason))
    {
        event_log_append(EVT_CONFIG_REJECT, (uint8_t)reason);
        return 0xFE;
    }
    if (!config_policy_validate(&tmp, &reason))
    {
        event_log_append(EVT_CONFIG_REJECT, (uint8_t)reason);
        return 0xFE;
    }
    /* Enforce monotonic sequence; recalc CRC after bump. */
    tmp.seq = g_config_active.seq + 1u;
    tmp.crc32 = 0;
    tmp.crc32 = config_crc_expected(&tmp);

    g_config_staged = tmp;
    g_config_staged_valid = 1;
    return 0;
}

uint8_t config_commit_staged(const uint8_t *p, uint8_t len)
{
    if (!g_config_staged_valid)
        return 0xFD;
    config_reject_reason_t reason = CFG_REJECT_NONE;
    if (!config_validate_reason(&g_config_staged, 1, &reason))
    {
        event_log_append(EVT_CONFIG_REJECT, (uint8_t)reason);
        g_config_staged_valid = 0;
        return 0xFE;
    }
    config_commit_active(&g_config_staged);
    g_config_staged_valid = 0;

    if (len >= 1 && p && p[0])
        g_request_soft_reboot = 2; /* reboot to app */

    return 0;
}
