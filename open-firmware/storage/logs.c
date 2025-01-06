#include "storage/logs.h"

#include <stddef.h>

#include "app_data.h"
#include "control/control.h"
#include "drivers/spi_flash.h"
#include "platform/time.h"
#include "storage/flash_util.h"
#include "storage/layout.h"
#include "util/byteorder.h"
#include "util/crc32.h"

typedef struct {
    uint32_t ms;
    uint8_t  type;
    uint8_t  flags;
    int16_t  speed_dmph;
    int16_t  batt_dV;
    int16_t  batt_dA;
    int16_t  temp_dC;
    uint16_t cmd_power_w;
    uint16_t cmd_current_dA;
    uint16_t crc16;
} event_record_t;

typedef struct {
    uint8_t  version;
    uint8_t  flags;
    uint16_t dt_ms;
    uint16_t speed_dmph;
    uint16_t cadence_rpm;
    uint16_t power_w;
    int16_t  batt_dV;
    int16_t  batt_dA;
    int16_t  temp_dC;
    uint8_t  assist_mode;
    uint8_t  profile_id;
    uint16_t crc16;
} stream_record_t;

event_log_meta_t g_event_meta;
stream_log_meta_t g_stream_meta;

uint8_t g_stream_log_enabled;
uint16_t g_stream_log_period_ms;
uint32_t g_stream_log_last_ms;
uint32_t g_stream_log_last_sample_ms;

static uint8_t is_all_ff(const uint8_t *buf, size_t len)
{
    if (!buf || len == 0)
        return 1;
    for (size_t i = 0; i < len; ++i)
    {
        if (buf[i] != 0xFFu)
            return 0;
    }
    return 1;
}

static uint16_t record_crc16_be(const uint8_t *buf, size_t size)
{
    if (!buf || size < 2u)
        return 0;
    return (uint16_t)(crc32_compute(buf, size - 2u) & 0xFFFFu);
}

static uint8_t event_record_valid_be(const uint8_t *buf)
{
    if (!buf)
        return 0;
    if (is_all_ff(buf, EVENT_LOG_RECORD_SIZE))
        return 0;
    uint16_t crc_expected = load_be16(&buf[EVENT_LOG_RECORD_SIZE - 2u]);
    uint16_t crc_actual = record_crc16_be(buf, EVENT_LOG_RECORD_SIZE);
    return crc_expected == crc_actual;
}

static void event_record_store(uint8_t *dst, const event_record_t *r)
{
    if (!dst || !r)
        return;
    store_be32(&dst[0], r->ms);
    dst[4] = r->type;
    dst[5] = r->flags;
    store_be16(&dst[6], (uint16_t)r->speed_dmph);
    store_be16(&dst[8], (uint16_t)r->batt_dV);
    store_be16(&dst[10], (uint16_t)r->batt_dA);
    store_be16(&dst[12], (uint16_t)r->temp_dC);
    store_be16(&dst[14], r->cmd_power_w);
    store_be16(&dst[16], r->cmd_current_dA);
    store_be16(&dst[18], r->crc16);
}

void event_log_reset(void)
{
    spi_flash_erase_region(EVENT_LOG_STORAGE_BASE, EVENT_LOG_STORAGE_BYTES);
    g_event_meta.magic = EVENT_LOG_MAGIC;
    g_event_meta.version = EVENT_LOG_VERSION;
    g_event_meta.record_size = EVENT_LOG_RECORD_SIZE;
    g_event_meta.capacity = EVENT_LOG_CAPACITY;
    g_event_meta.reserved = 0;
    g_event_meta.head = 0;
    g_event_meta.count = 0;
    g_event_meta.seq = 1;
    g_event_meta.crc32 = 0;
}

void event_log_load(void)
{
    /* Reset RAM state first. We will only keep flash content if it scans cleanly. */
    g_event_meta.magic = EVENT_LOG_MAGIC;
    g_event_meta.version = EVENT_LOG_VERSION;
    g_event_meta.record_size = EVENT_LOG_RECORD_SIZE;
    g_event_meta.capacity = EVENT_LOG_CAPACITY;
    g_event_meta.reserved = 0;
    g_event_meta.head = 0;
    g_event_meta.count = 0;
    g_event_meta.seq = 1;
    g_event_meta.crc32 = 0;

    uint8_t buf[EVENT_LOG_RECORD_SIZE];
    for (uint32_t i = 0; i < EVENT_LOG_CAPACITY; ++i)
    {
        spi_flash_read(EVENT_LOG_STORAGE_BASE + i * EVENT_LOG_RECORD_SIZE, buf, EVENT_LOG_RECORD_SIZE);
        if (is_all_ff(buf, EVENT_LOG_RECORD_SIZE))
            return;
        if (!event_record_valid_be(buf))
        {
            /* Corrupt/partial write: discard and start fresh so future writes succeed. */
            event_log_reset();
            return;
        }
        g_event_meta.head = i + 1u;
        g_event_meta.count = i + 1u;
        g_event_meta.seq += 1u;
    }
}

void event_log_append(uint8_t type, uint8_t flags)
{
    if (g_event_meta.head >= EVENT_LOG_CAPACITY)
        event_log_reset();

    event_record_t r;
    r.ms = g_ms;
    r.type = type;
    r.flags = flags;
    r.speed_dmph = (int16_t)g_inputs.speed_dmph;
    r.batt_dV = (int16_t)g_inputs.battery_dV;
    r.batt_dA = (int16_t)g_inputs.battery_dA;
    r.temp_dC = (int16_t)g_inputs.ctrl_temp_dC;
    r.cmd_power_w = g_outputs.cmd_power_w;
    r.cmd_current_dA = g_outputs.cmd_current_dA;
    r.crc16 = 0;

    uint8_t buf[EVENT_LOG_RECORD_SIZE];
    event_record_store(buf, &r);
    store_be16(&buf[EVENT_LOG_RECORD_SIZE - 2u], record_crc16_be(buf, EVENT_LOG_RECORD_SIZE));

    uint32_t idx = g_event_meta.head;
    spi_flash_write(EVENT_LOG_STORAGE_BASE + idx * EVENT_LOG_RECORD_SIZE, buf, EVENT_LOG_RECORD_SIZE);

    g_event_meta.head = g_event_meta.head + 1u;
    if (g_event_meta.count < EVENT_LOG_CAPACITY)
        g_event_meta.count++;
    g_event_meta.seq++;
    g_event_meta.crc32 = 0;
}

static uint8_t log_copy_records(uint32_t base, uint16_t record_size, uint32_t count,
                                uint16_t offset, uint8_t max_records, uint8_t *out)
{
    if (!out || max_records == 0)
        return 0;
    if (offset >= count)
        return 0;

    uint16_t available = (uint16_t)(count - offset);
    uint8_t n = (available < max_records) ? (uint8_t)available : max_records;

    for (uint8_t i = 0; i < n; ++i)
    {
        uint32_t idx = (uint32_t)offset + (uint32_t)i;
        spi_flash_read(base + idx * (uint32_t)record_size,
                       &out[(size_t)i * record_size],
                       record_size);
    }
    return n;
}

uint8_t event_log_copy(uint16_t offset, uint8_t max_records, uint8_t *out)
{
    return log_copy_records(EVENT_LOG_STORAGE_BASE, EVENT_LOG_RECORD_SIZE,
                            g_event_meta.count, offset, max_records, out);
}

uint16_t stream_log_period_sanitize(uint16_t period)
{
    if (period < STREAM_LOG_PERIOD_MIN_MS)
        return STREAM_LOG_PERIOD_MIN_MS;
    if (period > STREAM_LOG_PERIOD_MAX_MS)
        return STREAM_LOG_PERIOD_MAX_MS;
    return period;
}

void stream_log_reset(void)
{
    spi_flash_erase_region(STREAM_LOG_STORAGE_BASE, STREAM_LOG_STORAGE_BYTES);
    g_stream_meta.magic = STREAM_LOG_MAGIC;
    g_stream_meta.version = STREAM_LOG_VERSION;
    g_stream_meta.record_size = STREAM_LOG_RECORD_SIZE;
    g_stream_meta.capacity = STREAM_LOG_CAPACITY;
    g_stream_meta.reserved = 0;
    g_stream_meta.head = 0;
    g_stream_meta.count = 0;
    g_stream_meta.seq = 1;
    g_stream_meta.crc32 = 0;
}

void stream_log_load(void)
{
    /* Reset RAM state first. We will only keep flash content if it scans cleanly. */
    g_stream_meta.magic = STREAM_LOG_MAGIC;
    g_stream_meta.version = STREAM_LOG_VERSION;
    g_stream_meta.record_size = STREAM_LOG_RECORD_SIZE;
    g_stream_meta.capacity = STREAM_LOG_CAPACITY;
    g_stream_meta.reserved = 0;
    g_stream_meta.head = 0;
    g_stream_meta.count = 0;
    g_stream_meta.seq = 1;
    g_stream_meta.crc32 = 0;

    uint8_t buf[STREAM_LOG_RECORD_SIZE];
    for (uint32_t i = 0; i < STREAM_LOG_CAPACITY; ++i)
    {
        spi_flash_read(STREAM_LOG_STORAGE_BASE + i * STREAM_LOG_RECORD_SIZE, buf, STREAM_LOG_RECORD_SIZE);
        if (is_all_ff(buf, STREAM_LOG_RECORD_SIZE))
            return;
        uint16_t crc_expected = load_be16(&buf[STREAM_LOG_RECORD_SIZE - 2u]);
        uint16_t crc_actual = record_crc16_be(buf, STREAM_LOG_RECORD_SIZE);
        if (crc_expected != crc_actual)
        {
            stream_log_reset();
            return;
        }
        g_stream_meta.head = i + 1u;
        g_stream_meta.count = i + 1u;
        g_stream_meta.seq += 1u;
    }
}

static void stream_record_store(uint8_t *dst, const stream_record_t *r)
{
    if (!dst || !r)
        return;
    dst[0] = r->version;
    dst[1] = r->flags;
    store_be16(&dst[2], r->dt_ms);
    store_be16(&dst[4], r->speed_dmph);
    store_be16(&dst[6], r->cadence_rpm);
    store_be16(&dst[8], r->power_w);
    store_be16(&dst[10], (uint16_t)r->batt_dV);
    store_be16(&dst[12], (uint16_t)r->batt_dA);
    store_be16(&dst[14], (uint16_t)r->temp_dC);
    dst[16] = r->assist_mode;
    dst[17] = r->profile_id;
    store_be16(&dst[18], r->crc16);
}

void stream_log_append(uint8_t flags)
{
    if (g_stream_meta.head >= STREAM_LOG_CAPACITY)
        stream_log_reset();

    stream_record_t r;
    uint32_t now = g_ms;
    uint32_t dt = (g_stream_log_last_sample_ms == 0) ? 0u : (now - g_stream_log_last_sample_ms);
    if (dt > 0xFFFFu)
        dt = 0xFFFFu;
    r.version = STREAM_LOG_VERSION;
    r.flags = flags;
    r.dt_ms = (uint16_t)dt;
    r.speed_dmph = g_inputs.speed_dmph;
    r.cadence_rpm = g_inputs.cadence_rpm;
    r.power_w = g_inputs.power_w;
    r.batt_dV = g_inputs.battery_dV;
    r.batt_dA = g_inputs.battery_dA;
    r.temp_dC = g_inputs.ctrl_temp_dC;
    r.assist_mode = g_outputs.assist_mode;
    r.profile_id = g_outputs.profile_id;
    r.crc16 = 0;

    uint8_t buf[STREAM_LOG_RECORD_SIZE];
    stream_record_store(buf, &r);
    store_be16(&buf[STREAM_LOG_RECORD_SIZE - 2u], record_crc16_be(buf, STREAM_LOG_RECORD_SIZE));

    uint32_t idx = g_stream_meta.head;
    spi_flash_write(STREAM_LOG_STORAGE_BASE + idx * STREAM_LOG_RECORD_SIZE, buf, STREAM_LOG_RECORD_SIZE);

    g_stream_meta.head = g_stream_meta.head + 1u;
    if (g_stream_meta.count < STREAM_LOG_CAPACITY)
        g_stream_meta.count++;
    g_stream_meta.seq++;
    g_stream_meta.crc32 = 0;

    g_stream_log_last_sample_ms = now;
}

uint8_t stream_log_copy(uint16_t offset, uint8_t max_records, uint8_t *out)
{
    return log_copy_records(STREAM_LOG_STORAGE_BASE, STREAM_LOG_RECORD_SIZE,
                            g_stream_meta.count, offset, max_records, out);
}

void stream_log_tick(void)
{
    if (!g_stream_log_enabled)
        return;
    if (g_stream_log_period_ms == 0)
        return;
    uint32_t now = g_ms;
    if ((uint32_t)(now - g_stream_log_last_ms) >= g_stream_log_period_ms)
    {
        g_stream_log_last_ms = now;
        uint8_t flags = 0u;
        if (g_inputs.brake)
            flags |= 0x01u;
        if (g_walk_state == WALK_STATE_ACTIVE)
            flags |= 0x02u;
        stream_log_append(flags);
    }
}
