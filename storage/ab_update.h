#ifndef OPEN_FIRMWARE_STORAGE_AB_UPDATE_H
#define OPEN_FIRMWARE_STORAGE_AB_UPDATE_H

#include <stdint.h>

#include "storage/layout.h"

/* A/B update slots (SPI flash staging) */
#define AB_SLOT_NONE 0xFFu
#define AB_META_MAGIC 0x41424D54u /* 'ABMT' */
#define AB_META_VERSION 1u
#define AB_META_SIZE 24u
#define AB_META_STRIDE 64u
#define AB_META_COPIES 2u

#define AB_SLOT_MAGIC 0x4142534Cu /* 'ABSL' */
#define AB_SLOT_VERSION 1u
#define AB_SLOT_HEADER_SIZE 32u
#define AB_SLOT_MAX_IMAGE (AB_SLOT_STRIDE - AB_SLOT_HEADER_SIZE)

typedef struct {
    uint32_t seq;
    uint8_t  active_slot;
    uint8_t  pending_slot;
    uint8_t  last_good_slot;
    uint8_t  flags;
} ab_meta_t;

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t header_size;
    uint32_t image_size;
    uint32_t crc32;
    uint32_t build_id;
    uint32_t flags;
    uint32_t reserved0;
    uint32_t reserved1;
} ab_slot_hdr_t;

extern uint8_t g_ab_active_slot;
extern uint8_t g_ab_pending_slot;
extern uint8_t g_ab_last_good_slot;
extern uint8_t g_ab_active_valid;
extern uint8_t g_ab_pending_valid;
extern uint32_t g_ab_active_build_id;

uint8_t ab_slot_valid(uint8_t slot);
int ab_slot_read_header(uint8_t slot, ab_slot_hdr_t *out);

void ab_update_init(void);
uint8_t ab_update_set_pending(uint8_t slot);

#endif

