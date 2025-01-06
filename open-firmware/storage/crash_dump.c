#include "storage/crash_dump.h"

#include <stddef.h>

#include "drivers/spi_flash.h"
#include "platform/hw.h"
#include "platform/mmio.h"
#include "platform/time.h"
#include "storage/layout.h"
#include "util/byteorder.h"
#include "util/crc32.h"

static uint8_t g_crash_dump_buf[CRASH_DUMP_SIZE];
static uint32_t g_crash_dump_seq;

static void crash_dump_write(const uint8_t *buf)
{
    if (!buf)
        return;
    spi_flash_erase_4k(CRASH_DUMP_STORAGE_BASE);
    spi_flash_write(CRASH_DUMP_STORAGE_BASE, buf, CRASH_DUMP_SIZE);
}

static void crash_dump_read(uint8_t *out)
{
    if (!out)
        return;
    spi_flash_read(CRASH_DUMP_STORAGE_BASE, out, CRASH_DUMP_SIZE);
}

static void crash_dump_zero(uint8_t *buf, size_t len)
{
    if (!buf)
        return;
    for (size_t i = 0; i < len; ++i)
        buf[i] = 0;
}

static uint32_t crash_dump_crc32(const uint8_t *buf)
{
    uint8_t tmp[CRASH_DUMP_SIZE];
    for (size_t i = 0; i < CRASH_DUMP_SIZE; ++i)
        tmp[i] = buf[i];
    store_be32(&tmp[CRASH_DUMP_OFF_CRC], 0);
    return crc32_compute(tmp, CRASH_DUMP_SIZE);
}

static int crash_dump_valid(const uint8_t *buf)
{
    if (!buf)
        return 0;
    if (load_be32(&buf[CRASH_DUMP_OFF_MAGIC]) != CRASH_DUMP_MAGIC)
        return 0;
    if (load_be16(&buf[CRASH_DUMP_OFF_VERSION]) != CRASH_DUMP_VERSION)
        return 0;
    if (load_be16(&buf[CRASH_DUMP_OFF_SIZE]) != CRASH_DUMP_SIZE)
        return 0;
    uint32_t crc_expected = load_be32(&buf[CRASH_DUMP_OFF_CRC]);
    uint32_t crc_actual = crash_dump_crc32(buf);
    return crc_expected == crc_actual;
}

uint8_t crash_dump_load(uint8_t *out)
{
    crash_dump_read(out);
    if (!crash_dump_valid(out))
    {
        crash_dump_zero(out, CRASH_DUMP_SIZE);
        return 0;
    }
    return 1;
}

void crash_dump_clear_storage(void)
{
    crash_dump_zero(g_crash_dump_buf, CRASH_DUMP_SIZE);
    crash_dump_write(g_crash_dump_buf);
}

void crash_dump_capture(uint32_t sp, uint32_t lr, uint32_t pc, uint32_t psr)
{
    crash_dump_zero(g_crash_dump_buf, CRASH_DUMP_SIZE);
    store_be32(&g_crash_dump_buf[CRASH_DUMP_OFF_MAGIC], CRASH_DUMP_MAGIC);
    store_be16(&g_crash_dump_buf[CRASH_DUMP_OFF_VERSION], CRASH_DUMP_VERSION);
    store_be16(&g_crash_dump_buf[CRASH_DUMP_OFF_SIZE], CRASH_DUMP_SIZE);
    store_be32(&g_crash_dump_buf[CRASH_DUMP_OFF_FLAGS], 0);
    g_crash_dump_seq++;
    store_be32(&g_crash_dump_buf[CRASH_DUMP_OFF_SEQ], g_crash_dump_seq);
    store_be32(&g_crash_dump_buf[CRASH_DUMP_OFF_MS], g_ms);
    store_be32(&g_crash_dump_buf[CRASH_DUMP_OFF_SP], sp);
    store_be32(&g_crash_dump_buf[CRASH_DUMP_OFF_LR], lr);
    store_be32(&g_crash_dump_buf[CRASH_DUMP_OFF_PC], pc);
    store_be32(&g_crash_dump_buf[CRASH_DUMP_OFF_PSR], psr);
    store_be32(&g_crash_dump_buf[CRASH_DUMP_OFF_CFSR], mmio_read32(SCB_CFSR));
    store_be32(&g_crash_dump_buf[CRASH_DUMP_OFF_HFSR], mmio_read32(SCB_HFSR));
    store_be32(&g_crash_dump_buf[CRASH_DUMP_OFF_DFSR], mmio_read32(SCB_DFSR));
    store_be32(&g_crash_dump_buf[CRASH_DUMP_OFF_MMFAR], mmio_read32(SCB_MMFAR));
    store_be32(&g_crash_dump_buf[CRASH_DUMP_OFF_BFAR], mmio_read32(SCB_BFAR));
    store_be32(&g_crash_dump_buf[CRASH_DUMP_OFF_AFSR], mmio_read32(SCB_AFSR));

    uint32_t count = g_event_meta.count;
    uint8_t want = (count < CRASH_DUMP_EVENT_MAX) ? (uint8_t)count : (uint8_t)CRASH_DUMP_EVENT_MAX;
    uint16_t offset = (count > want) ? (uint16_t)(count - want) : 0u;
    uint8_t got = 0;
    if (want)
        got = event_log_copy(offset, want, &g_crash_dump_buf[CRASH_DUMP_OFF_EVENT_RECORDS]);
    store_be16(&g_crash_dump_buf[CRASH_DUMP_OFF_EVENT_COUNT], got);
    store_be16(&g_crash_dump_buf[CRASH_DUMP_OFF_EVENT_REC_SIZE], EVENT_LOG_RECORD_SIZE);
    store_be32(&g_crash_dump_buf[CRASH_DUMP_OFF_EVENT_SEQ], g_event_meta.seq);

    store_be32(&g_crash_dump_buf[CRASH_DUMP_OFF_CRC], 0);
    uint32_t crc = crc32_compute(g_crash_dump_buf, CRASH_DUMP_SIZE);
    store_be32(&g_crash_dump_buf[CRASH_DUMP_OFF_CRC], crc);
    crash_dump_write(g_crash_dump_buf);
}
