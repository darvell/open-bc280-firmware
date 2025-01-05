/*
 * Unit Tests for A/B update metadata handling.
 */

#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "storage/ab_update.h"
#include "storage/layout.h"
#include "util/byteorder.h"
#include "util/crc32.h"

#define FLASH_SIZE ((size_t)AB_SLOT1_BASE + (size_t)AB_SLOT_STRIDE)

static uint8_t s_flash[FLASH_SIZE];

uint8_t g_ab_active_slot;
uint8_t g_ab_pending_slot;
uint8_t g_ab_last_good_slot;
uint8_t g_ab_active_valid;
uint8_t g_ab_pending_valid;
uint32_t g_ab_active_build_id;

void spi_flash_read(uint32_t addr, uint8_t *out, uint32_t len)
{
    if ((size_t)addr + (size_t)len > sizeof(s_flash))
    {
        memset(out, 0, len);
        return;
    }
    memcpy(out, &s_flash[addr], len);
}

void spi_flash_update_bytes(uint32_t addr, const uint8_t *data, uint32_t len)
{
    if ((size_t)addr + (size_t)len > sizeof(s_flash))
        return;
    memcpy(&s_flash[addr], data, len);
}

#define TEST(name) static void test_##name(void)
#define RUN_TEST(name) do { \
    printf("  %-50s ", #name); \
    test_##name(); \
    printf("PASS\n"); \
    tests_passed++; \
} while(0)

#define ASSERT_TRUE(cond) do { \
    if (!(cond)) { \
        printf("FAIL\n    Assertion failed: %s\n    at %s:%d\n", \
               #cond, __FILE__, __LINE__); \
        tests_failed++; \
        return; \
    } \
} while(0)

static int tests_passed = 0;
static int tests_failed = 0;

static void flash_reset(void)
{
    memset(s_flash, 0xFF, sizeof(s_flash));
}

static void write_meta_copy(uint8_t idx, uint32_t seq, uint8_t active, uint8_t pending,
                            uint8_t last_good, uint8_t flags)
{
    uint8_t buf[AB_META_SIZE];
    store_be32(&buf[0], AB_META_MAGIC);
    store_be16(&buf[4], AB_META_VERSION);
    store_be16(&buf[6], AB_META_SIZE);
    store_be32(&buf[8], seq);
    buf[12] = active;
    buf[13] = pending;
    buf[14] = last_good;
    buf[15] = flags;
    store_be32(&buf[16], 0u);
    store_be32(&buf[20], 0u);
    uint32_t crc = crc32_compute(buf, AB_META_SIZE);
    store_be32(&buf[20], crc);
    uint32_t base = AB_META_BASE + (uint32_t)idx * AB_META_STRIDE;
    spi_flash_update_bytes(base, buf, AB_META_SIZE);
}

static void write_slot_image(uint8_t slot, const uint8_t *payload, uint32_t len,
                             uint32_t build_id, uint8_t valid_crc)
{
    uint8_t header[AB_SLOT_HEADER_SIZE];
    uint32_t base = (slot == 0u) ? AB_SLOT0_BASE : AB_SLOT1_BASE;
    uint32_t crc = crc32_compute(payload, len);
    if (!valid_crc)
        crc ^= 0xFFFFFFFFu;
    store_be32(&header[0], AB_SLOT_MAGIC);
    store_be16(&header[4], AB_SLOT_VERSION);
    store_be16(&header[6], AB_SLOT_HEADER_SIZE);
    store_be32(&header[8], len);
    store_be32(&header[12], crc);
    store_be32(&header[16], build_id);
    store_be32(&header[20], 0u);
    store_be32(&header[24], 0u);
    store_be32(&header[28], 0u);
    spi_flash_update_bytes(base, header, AB_SLOT_HEADER_SIZE);
    spi_flash_update_bytes(base + AB_SLOT_HEADER_SIZE, payload, len);
}

typedef struct
{
    uint32_t seq;
    uint8_t active_slot;
    uint8_t pending_slot;
    uint8_t last_good_slot;
    uint8_t flags;
} test_meta_t;

static uint32_t meta_crc_buf(const uint8_t *buf)
{
    uint8_t tmp[AB_META_SIZE];
    memcpy(tmp, buf, AB_META_SIZE);
    store_be32(&tmp[20], 0);
    return crc32_compute(tmp, AB_META_SIZE);
}

static int read_meta_copy(uint8_t idx, test_meta_t *out)
{
    uint8_t buf[AB_META_SIZE];
    uint32_t base = AB_META_BASE + (uint32_t)idx * AB_META_STRIDE;
    spi_flash_read(base, buf, AB_META_SIZE);
    if (load_be32(&buf[0]) != AB_META_MAGIC)
        return 0;
    if (load_be16(&buf[4]) != AB_META_VERSION)
        return 0;
    if (load_be16(&buf[6]) != AB_META_SIZE)
        return 0;
    if (load_be32(&buf[20]) != meta_crc_buf(buf))
        return 0;
    if (out)
    {
        out->seq = load_be32(&buf[8]);
        out->active_slot = buf[12];
        out->pending_slot = buf[13];
        out->last_good_slot = buf[14];
        out->flags = buf[15];
    }
    return 1;
}

static int read_meta_best(test_meta_t *out)
{
    test_meta_t best = {0};
    uint8_t found = 0;
    for (uint8_t i = 0; i < AB_META_COPIES; ++i)
    {
        test_meta_t tmp;
        if (read_meta_copy(i, &tmp))
        {
            if (!found || tmp.seq > best.seq)
            {
                best = tmp;
                found = 1u;
            }
        }
    }
    if (out && found)
        *out = best;
    return found ? 1 : 0;
}

TEST(invalid_pending_slot_does_not_replace_active)
{
    flash_reset();

    const uint8_t payload[4] = {0x10, 0x20, 0x30, 0x40};
    write_meta_copy(0, 1u, 0u, 1u, 0u, 0u);
    write_slot_image(0u, payload, sizeof(payload), 0x12345678u, 1u);
    write_slot_image(1u, payload, sizeof(payload), 0xDEADBEEFu, 0u);

    ab_update_init();

    ASSERT_TRUE(g_ab_active_slot == 0u);
    ASSERT_TRUE(g_ab_last_good_slot == 0u);
    ASSERT_TRUE(g_ab_pending_slot == AB_SLOT_NONE);
    ASSERT_TRUE(g_ab_active_valid == 1u);
    ASSERT_TRUE(g_ab_pending_valid == 0u);
    ASSERT_TRUE(g_ab_active_build_id == 0x12345678u);
}

TEST(valid_pending_slot_replaces_active)
{
    flash_reset();

    const uint8_t payload[4] = {0xA1, 0xB2, 0xC3, 0xD4};
    write_meta_copy(0, 5u, 0u, 1u, 0u, 0u);
    write_slot_image(0u, payload, sizeof(payload), 0x0A0A0A0Au, 1u);
    write_slot_image(1u, payload, sizeof(payload), 0x0B0B0B0Bu, 1u);

    ab_update_init();

    ASSERT_TRUE(g_ab_active_slot == 1u);
    ASSERT_TRUE(g_ab_last_good_slot == 0u);
    ASSERT_TRUE(g_ab_pending_slot == AB_SLOT_NONE);
    ASSERT_TRUE(g_ab_active_valid == 1u);
    ASSERT_TRUE(g_ab_pending_valid == 0u);
    ASSERT_TRUE(g_ab_active_build_id == 0x0B0B0B0Bu);
}

TEST(invalid_active_slot_sanitizes_to_zero)
{
    flash_reset();

    const uint8_t payload[4] = {0x01, 0x02, 0x03, 0x04};
    write_meta_copy(0, 4u, 2u, AB_SLOT_NONE, 2u, 0u);
    write_slot_image(0u, payload, sizeof(payload), 0x01020304u, 1u);

    ab_update_init();

    ASSERT_TRUE(g_ab_active_slot == 0u);
    ASSERT_TRUE(g_ab_last_good_slot == 0u);
    ASSERT_TRUE(g_ab_pending_slot == AB_SLOT_NONE);
}

TEST(invalid_pending_slot_clears_meta_pending)
{
    flash_reset();

    const uint8_t payload[4] = {0x55, 0x66, 0x77, 0x88};
    write_meta_copy(0, 3u, 0u, 1u, 0u, 0u);
    write_slot_image(0u, payload, sizeof(payload), 0x0D0D0D0Du, 1u);
    write_slot_image(1u, payload, sizeof(payload), 0x0E0E0E0Eu, 0u);

    ab_update_init();

    test_meta_t meta;
    ASSERT_TRUE(read_meta_best(&meta) == 1);
    ASSERT_TRUE(meta.seq == 4u);
    ASSERT_TRUE(meta.pending_slot == AB_SLOT_NONE);
}

TEST(invalid_pending_slot_preserves_last_good)
{
    flash_reset();

    const uint8_t payload[4] = {0x41, 0x42, 0x43, 0x44};
    write_meta_copy(0, 6u, 1u, 0u, 0u, 0u);
    write_slot_image(0u, payload, sizeof(payload), 0x11111111u, 0u);
    write_slot_image(1u, payload, sizeof(payload), 0x22222222u, 1u);

    ab_update_init();

    ASSERT_TRUE(g_ab_active_slot == 1u);
    ASSERT_TRUE(g_ab_last_good_slot == 0u);
    ASSERT_TRUE(g_ab_pending_slot == AB_SLOT_NONE);
    ASSERT_TRUE(g_ab_active_valid == 1u);
    ASSERT_TRUE(g_ab_pending_valid == 0u);

    test_meta_t meta;
    ASSERT_TRUE(read_meta_best(&meta) == 1);
    ASSERT_TRUE(meta.pending_slot == AB_SLOT_NONE);
    ASSERT_TRUE(meta.last_good_slot == 0u);
}

TEST(fresh_meta_written_on_empty_flash)
{
    flash_reset();

    ab_update_init();

    ASSERT_TRUE(g_ab_active_slot == 0u);
    ASSERT_TRUE(g_ab_last_good_slot == 0u);
    ASSERT_TRUE(g_ab_pending_slot == AB_SLOT_NONE);
    ASSERT_TRUE(g_ab_active_valid == 0u);
    ASSERT_TRUE(g_ab_pending_valid == 0u);
    ASSERT_TRUE(g_ab_active_build_id == 0u);

    test_meta_t meta;
    ASSERT_TRUE(read_meta_best(&meta) == 1);
    ASSERT_TRUE(meta.seq == 1u);
    ASSERT_TRUE(meta.active_slot == 0u);
    ASSERT_TRUE(meta.pending_slot == AB_SLOT_NONE);
    ASSERT_TRUE(meta.last_good_slot == 0u);
    ASSERT_TRUE(meta.flags == 0u);
}

TEST(pending_valid_overrides_invalid_active_header)
{
    flash_reset();

    const uint8_t payload[4] = {0x22, 0x33, 0x44, 0x55};
    write_meta_copy(0, 7u, 0u, 1u, 0u, 0u);
    write_slot_image(0u, payload, sizeof(payload), 0x10101010u, 0u);
    write_slot_image(1u, payload, sizeof(payload), 0x20202020u, 1u);

    ab_update_init();

    ASSERT_TRUE(g_ab_active_slot == 1u);
    ASSERT_TRUE(g_ab_last_good_slot == 0u);
    ASSERT_TRUE(g_ab_pending_slot == AB_SLOT_NONE);
    ASSERT_TRUE(g_ab_active_valid == 1u);
    ASSERT_TRUE(g_ab_pending_valid == 0u);
    ASSERT_TRUE(g_ab_active_build_id == 0x20202020u);
}

TEST(set_pending_rejects_invalid_slot)
{
    flash_reset();

    write_meta_copy(0, 1u, 0u, AB_SLOT_NONE, 0u, 0u);

    g_ab_pending_slot = 0xAAu;
    g_ab_pending_valid = 1u;

    ASSERT_TRUE(ab_update_set_pending(3u) == 0xFE);
    ASSERT_TRUE(g_ab_pending_slot == 0xAAu);
    ASSERT_TRUE(g_ab_pending_valid == 1u);
}

TEST(set_pending_none_clears_pending)
{
    flash_reset();

    const uint8_t payload[4] = {0x11, 0x22, 0x33, 0x44};
    write_slot_image(1u, payload, sizeof(payload), 0x13572468u, 1u);

    ASSERT_TRUE(ab_update_set_pending(1u) == 0u);
    ASSERT_TRUE(g_ab_pending_slot == 1u);
    ASSERT_TRUE(g_ab_pending_valid == 1u);

    ASSERT_TRUE(ab_update_set_pending(AB_SLOT_NONE) == 0u);
    ASSERT_TRUE(g_ab_pending_slot == AB_SLOT_NONE);
    ASSERT_TRUE(g_ab_pending_valid == 0u);
}

TEST(set_pending_none_updates_meta)
{
    flash_reset();

    write_meta_copy(0, 8u, 0u, 1u, 0u, 0u);

    ASSERT_TRUE(ab_update_set_pending(AB_SLOT_NONE) == 0u);
    ASSERT_TRUE(g_ab_pending_slot == AB_SLOT_NONE);
    ASSERT_TRUE(g_ab_pending_valid == 0u);

    test_meta_t meta;
    ASSERT_TRUE(read_meta_best(&meta) == 1);
    ASSERT_TRUE(meta.seq == 9u);
    ASSERT_TRUE(meta.active_slot == 0u);
    ASSERT_TRUE(meta.pending_slot == AB_SLOT_NONE);
    ASSERT_TRUE(meta.last_good_slot == 0u);
}

TEST(set_pending_rejects_active_slot)
{
    flash_reset();

    const uint8_t payload[4] = {0x12, 0x34, 0x56, 0x78};
    write_meta_copy(0, 2u, 0u, AB_SLOT_NONE, 0u, 0u);
    write_slot_image(0u, payload, sizeof(payload), 0x0C0FFEEu, 1u);

    ab_update_init();

    ASSERT_TRUE(ab_update_set_pending(0u) == 0u);
    ASSERT_TRUE(g_ab_pending_slot == AB_SLOT_NONE);
    ASSERT_TRUE(g_ab_pending_valid == 0u);
}

TEST(set_pending_invalid_header_marks_pending_invalid)
{
    flash_reset();

    ASSERT_TRUE(ab_update_set_pending(1u) == 0u);
    ASSERT_TRUE(g_ab_pending_slot == 1u);
    ASSERT_TRUE(g_ab_pending_valid == 0u);
}

TEST(set_pending_updates_meta)
{
    flash_reset();

    const uint8_t payload[4] = {0x90, 0x91, 0x92, 0x93};
    write_meta_copy(0, 4u, 0u, AB_SLOT_NONE, 0u, 0u);
    write_slot_image(1u, payload, sizeof(payload), 0x42424242u, 1u);

    ASSERT_TRUE(ab_update_set_pending(1u) == 0u);
    ASSERT_TRUE(g_ab_pending_slot == 1u);
    ASSERT_TRUE(g_ab_pending_valid == 1u);

    test_meta_t meta;
    ASSERT_TRUE(read_meta_best(&meta) == 1);
    ASSERT_TRUE(meta.seq == 5u);
    ASSERT_TRUE(meta.active_slot == 0u);
    ASSERT_TRUE(meta.pending_slot == 1u);
    ASSERT_TRUE(meta.last_good_slot == 0u);
}

TEST(set_pending_preserves_last_good)
{
    flash_reset();

    const uint8_t payload[4] = {0x10, 0x20, 0x30, 0x40};
    write_meta_copy(0, 2u, 1u, AB_SLOT_NONE, 0u, 0u);
    write_slot_image(0u, payload, sizeof(payload), 0x01020304u, 1u);

    ASSERT_TRUE(ab_update_set_pending(0u) == 0u);
    ASSERT_TRUE(g_ab_pending_slot == 0u);
    ASSERT_TRUE(g_ab_pending_valid == 1u);

    test_meta_t meta;
    ASSERT_TRUE(read_meta_best(&meta) == 1);
    ASSERT_TRUE(meta.seq == 3u);
    ASSERT_TRUE(meta.active_slot == 1u);
    ASSERT_TRUE(meta.pending_slot == 0u);
    ASSERT_TRUE(meta.last_good_slot == 0u);
}

TEST(set_pending_on_empty_flash_writes_meta)
{
    flash_reset();

    const uint8_t payload[4] = {0xDE, 0xAD, 0xBE, 0xEF};
    write_slot_image(1u, payload, sizeof(payload), 0x11223344u, 1u);

    ASSERT_TRUE(ab_update_set_pending(1u) == 0u);
    ASSERT_TRUE(g_ab_pending_slot == 1u);
    ASSERT_TRUE(g_ab_pending_valid == 1u);

    test_meta_t meta;
    ASSERT_TRUE(read_meta_best(&meta) == 1);
    ASSERT_TRUE(meta.seq == 2u);
    ASSERT_TRUE(meta.active_slot == 0u);
    ASSERT_TRUE(meta.pending_slot == 1u);
    ASSERT_TRUE(meta.last_good_slot == 0u);
}

TEST(pending_slot_matching_active_is_cleared)
{
    flash_reset();

    const uint8_t payload[4] = {0xCA, 0xFE, 0xBA, 0xBE};
    write_meta_copy(0, 9u, 0u, 0u, 0u, 0u);
    write_slot_image(0u, payload, sizeof(payload), 0xCAFEBABEu, 1u);

    ab_update_init();

    ASSERT_TRUE(g_ab_active_slot == 0u);
    ASSERT_TRUE(g_ab_pending_slot == AB_SLOT_NONE);
    ASSERT_TRUE(g_ab_pending_valid == 0u);
    ASSERT_TRUE(g_ab_active_valid == 1u);
    ASSERT_TRUE(g_ab_active_build_id == 0xCAFEBABEu);
}

int main(void)
{
    printf("\nA/B Update Unit Tests\n");
    printf("======================\n\n");

    RUN_TEST(invalid_pending_slot_does_not_replace_active);
    RUN_TEST(valid_pending_slot_replaces_active);
    RUN_TEST(invalid_active_slot_sanitizes_to_zero);
    RUN_TEST(invalid_pending_slot_clears_meta_pending);
    RUN_TEST(invalid_pending_slot_preserves_last_good);
    RUN_TEST(fresh_meta_written_on_empty_flash);
    RUN_TEST(pending_valid_overrides_invalid_active_header);
    RUN_TEST(set_pending_rejects_invalid_slot);
    RUN_TEST(set_pending_none_clears_pending);
    RUN_TEST(set_pending_none_updates_meta);
    RUN_TEST(set_pending_rejects_active_slot);
    RUN_TEST(set_pending_invalid_header_marks_pending_invalid);
    RUN_TEST(set_pending_updates_meta);
    RUN_TEST(set_pending_preserves_last_good);
    RUN_TEST(set_pending_on_empty_flash_writes_meta);
    RUN_TEST(pending_slot_matching_active_is_cleared);

    printf("\n");
    printf("======================\n");
    printf("Tests passed: %d\n", tests_passed);
    printf("Tests failed: %d\n", tests_failed);
    printf("======================\n\n");

    return tests_failed > 0 ? 1 : 0;
}
