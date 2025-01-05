#include "storage/ab_update.h"

#include <stddef.h>

#include "drivers/spi_flash.h"
#include "util/byteorder.h"
#include "util/crc32.h"

uint8_t g_ab_active_slot;
uint8_t g_ab_pending_slot;
uint8_t g_ab_last_good_slot;
uint8_t g_ab_active_valid;
uint8_t g_ab_pending_valid;
uint32_t g_ab_active_build_id;

static uint32_t ab_slot_base(uint8_t slot)
{
    return (slot == 0u) ? AB_SLOT0_BASE : AB_SLOT1_BASE;
}

uint8_t ab_slot_valid(uint8_t slot)
{
    return (slot <= 1u) ? 1u : 0u;
}

static void ab_meta_sanitize(ab_meta_t *m)
{
    if (!m)
        return;
    if (!ab_slot_valid(m->active_slot))
        m->active_slot = 0;
    if (!ab_slot_valid(m->last_good_slot))
        m->last_good_slot = m->active_slot;
    if (!ab_slot_valid(m->pending_slot))
        m->pending_slot = AB_SLOT_NONE;
    if (m->pending_slot == m->active_slot)
        m->pending_slot = AB_SLOT_NONE;
}

static uint32_t ab_meta_crc_buf(const uint8_t *buf)
{
    uint8_t tmp[AB_META_SIZE];
    for (uint16_t i = 0; i < AB_META_SIZE; ++i)
        tmp[i] = buf[i];
    store_be32(&tmp[20], 0);
    return crc32_compute(tmp, AB_META_SIZE);
}

static int ab_meta_read_copy(uint8_t idx, ab_meta_t *out)
{
    uint8_t buf[AB_META_SIZE];
    spi_flash_read(AB_META_BASE + (uint32_t)idx * AB_META_STRIDE, buf, AB_META_SIZE);
    if (load_be32(&buf[0]) != AB_META_MAGIC)
        return 0;
    if (load_be16(&buf[4]) != AB_META_VERSION)
        return 0;
    if (load_be16(&buf[6]) != AB_META_SIZE)
        return 0;
    uint32_t crc = load_be32(&buf[20]);
    if (crc != ab_meta_crc_buf(buf))
        return 0;
    if (out)
    {
        out->seq = load_be32(&buf[8]);
        out->active_slot = buf[12];
        out->pending_slot = buf[13];
        out->last_good_slot = buf[14];
        out->flags = buf[15];
        ab_meta_sanitize(out);
    }
    return 1;
}

static void ab_meta_load(ab_meta_t *out, uint8_t *fresh_out)
{
    ab_meta_t best = {0};
    uint8_t found = 0;
    for (uint8_t i = 0; i < AB_META_COPIES; ++i)
    {
        ab_meta_t tmp;
        if (ab_meta_read_copy(i, &tmp))
        {
            if (!found || tmp.seq > best.seq)
            {
                best = tmp;
                found = 1u;
            }
        }
    }
    if (!found)
    {
        best.seq = 1u;
        best.active_slot = 0u;
        best.pending_slot = AB_SLOT_NONE;
        best.last_good_slot = 0u;
        best.flags = 0u;
        if (fresh_out)
            *fresh_out = 1u;
    }
    else if (fresh_out)
    {
        *fresh_out = 0u;
    }
    ab_meta_sanitize(&best);
    if (out)
        *out = best;
}

static void ab_meta_write(const ab_meta_t *m)
{
    if (!m)
        return;
    uint8_t buf[AB_META_SIZE];
    store_be32(&buf[0], AB_META_MAGIC);
    store_be16(&buf[4], AB_META_VERSION);
    store_be16(&buf[6], AB_META_SIZE);
    store_be32(&buf[8], m->seq);
    buf[12] = m->active_slot;
    buf[13] = m->pending_slot;
    buf[14] = m->last_good_slot;
    buf[15] = m->flags;
    store_be32(&buf[16], 0);
    store_be32(&buf[20], 0);
    uint32_t crc = crc32_compute(buf, AB_META_SIZE);
    store_be32(&buf[20], crc);
    uint32_t slot = m->seq & 1u;
    spi_flash_update_bytes(AB_META_BASE + slot * AB_META_STRIDE, buf, AB_META_SIZE);
}

int ab_slot_read_header(uint8_t slot, ab_slot_hdr_t *out)
{
    if (!ab_slot_valid(slot))
        return 0;
    uint8_t buf[AB_SLOT_HEADER_SIZE];
    spi_flash_read(ab_slot_base(slot), buf, AB_SLOT_HEADER_SIZE);
    if (load_be32(&buf[0]) != AB_SLOT_MAGIC)
        return 0;
    uint16_t version = load_be16(&buf[4]);
    if (version != AB_SLOT_VERSION)
        return 0;
    uint16_t header_size = load_be16(&buf[6]);
    if (header_size < AB_SLOT_HEADER_SIZE)
        return 0;
    uint32_t image_size = load_be32(&buf[8]);
    if (image_size == 0 || image_size > AB_SLOT_MAX_IMAGE)
        return 0;
    if ((uint32_t)header_size + image_size > AB_SLOT_STRIDE)
        return 0;
    uint32_t crc_expected = load_be32(&buf[12]);
    uint32_t payload_addr = ab_slot_base(slot) + header_size;
    uint32_t remaining = image_size;
    uint8_t chunk[128];
    uint32_t crc = 0xFFFFFFFFu;
    while (remaining)
    {
        uint32_t n = remaining;
        if (n > (uint32_t)sizeof(chunk))
            n = (uint32_t)sizeof(chunk);
        spi_flash_read(payload_addr, chunk, n);
        crc = crc32_update(crc, chunk, n);
        payload_addr += n;
        remaining -= n;
    }
    uint32_t crc_actual = ~crc;
    if (crc_actual != crc_expected)
        return 0;
    if (out)
    {
        out->magic = AB_SLOT_MAGIC;
        out->version = version;
        out->header_size = header_size;
        out->image_size = image_size;
        out->crc32 = crc_expected;
        out->build_id = load_be32(&buf[16]);
        out->flags = load_be32(&buf[20]);
        out->reserved0 = load_be32(&buf[24]);
        out->reserved1 = load_be32(&buf[28]);
    }
    return 1;
}

void ab_update_init(void)
{
    ab_meta_t meta;
    uint8_t fresh = 0;
    ab_meta_load(&meta, &fresh);
    if (fresh)
        ab_meta_write(&meta);

    g_ab_active_slot = meta.active_slot;
    g_ab_pending_slot = meta.pending_slot;
    g_ab_last_good_slot = meta.last_good_slot;
    g_ab_active_valid = 0;
    g_ab_pending_valid = 0;
    g_ab_active_build_id = 0;

    ab_slot_hdr_t hdr;
    if (ab_slot_read_header(g_ab_active_slot, &hdr))
    {
        g_ab_active_valid = 1u;
        g_ab_active_build_id = hdr.build_id;
    }

    if (g_ab_pending_slot != AB_SLOT_NONE && ab_slot_valid(g_ab_pending_slot) &&
        g_ab_pending_slot != g_ab_active_slot)
    {
        if (ab_slot_read_header(g_ab_pending_slot, &hdr))
        {
            g_ab_last_good_slot = g_ab_active_slot;
            g_ab_active_slot = g_ab_pending_slot;
            g_ab_active_build_id = hdr.build_id;
            g_ab_active_valid = 1u;
            meta.seq += 1u;
            meta.active_slot = g_ab_active_slot;
            meta.pending_slot = AB_SLOT_NONE;
            meta.last_good_slot = g_ab_last_good_slot;
            ab_meta_write(&meta);
            g_ab_pending_slot = AB_SLOT_NONE;
        }
        else
        {
            meta.seq += 1u;
            meta.pending_slot = AB_SLOT_NONE;
            ab_meta_write(&meta);
            g_ab_pending_slot = AB_SLOT_NONE;
        }
    }

    if (ab_slot_valid(g_ab_pending_slot))
        g_ab_pending_valid = ab_slot_read_header(g_ab_pending_slot, &hdr) ? 1u : 0u;
    else
        g_ab_pending_valid = 0u;
}

uint8_t ab_update_set_pending(uint8_t slot)
{
    if (slot != AB_SLOT_NONE && !ab_slot_valid(slot))
        return 0xFE;

    ab_meta_t meta;
    uint8_t fresh = 0;
    ab_meta_load(&meta, &fresh);
    if (fresh)
        ab_meta_write(&meta);

    if (slot != AB_SLOT_NONE && slot == meta.active_slot)
        slot = AB_SLOT_NONE;

    meta.seq += 1u;
    meta.pending_slot = slot;
    ab_meta_write(&meta);

    g_ab_pending_slot = slot;
    if (slot != AB_SLOT_NONE && ab_slot_read_header(slot, NULL))
        g_ab_pending_valid = 1u;
    else
        g_ab_pending_valid = 0u;

    return 0;
}
