#ifndef SHENGYI_H
#define SHENGYI_H

#include <stdint.h>
#include <stddef.h>

/* Shengyi DWG22 frame opcodes */
#define SHENGYI_OPCODE_STATUS    0x52
#define SHENGYI_OPCODE_CONFIG_C2 0xC2
#define SHENGYI_OPCODE_CONFIG_C3 0xC3
#define SHENGYI_OPCODE_STATUS_C0 0xC0
#define SHENGYI_OPCODE_STATUS_14 0x14

/* Shengyi DWG22 frame constants */
#define SHENGYI_FRAME_START   0x3Au
#define SHENGYI_FRAME_SECOND  0x1Au
#define SHENGYI_FRAME_CR      0x0Du
#define SHENGYI_FRAME_LF      0x0Au

/* Motor status from 0x52 frame */
typedef struct {
    uint16_t rpm;
    uint16_t speed_dmph;
    uint16_t torque_raw;
    uint16_t power_w;
    int16_t battery_dV;
    int16_t battery_dA;
    int16_t ctrl_temp_dC;
    uint8_t soc_pct;
    uint8_t err;
    uint8_t assist_level;
} shengyi_motor_status_t;

/* API declarations */
void shengyi_init(void);
void shengyi_tick(void);

/* Send a 0x52 request frame over UART2 */
void shengyi_send_0x52_req(uint8_t assist_level_mapped,
                              uint8_t headlight_enabled,
                              uint8_t walk_assist_active,
                              uint8_t speed_over_limit);

/* Request state update to motor (force=1 sends immediately) */
void shengyi_request_update(uint8_t force);

/* Periodic tick - sends pending updates */
void shengyi_periodic_send_tick(void);

/* Calculate checksum for frame */
static inline uint16_t shengyi_checksum16(const uint8_t *buf, size_t len)
{
    uint32_t sum = 0;
    for (size_t i = 1; i + 4 < len; ++i)
        sum += buf[i];
    return (uint16_t)(sum & 0xFFFFu);
}

/* Build a generic Shengyi frame (cmd + payload + checksum + CR/LF). */
static inline size_t shengyi_frame_build(uint8_t cmd, const uint8_t *payload, uint8_t payload_len,
                                         uint8_t *out, size_t cap)
{
    if (!out)
        return 0;
    size_t total = (size_t)payload_len + 8u;
    if (cap < total)
        return 0;

    size_t len = 0;
    out[len++] = SHENGYI_FRAME_START;
    out[len++] = SHENGYI_FRAME_SECOND;
    out[len++] = cmd;
    out[len++] = payload_len;
    for (uint8_t i = 0; i < payload_len; ++i)
        out[len++] = payload ? payload[i] : 0u;
    uint16_t cks = shengyi_checksum16(out, len + 4u);
    out[len++] = (uint8_t)(cks & 0xFFu);
    out[len++] = (uint8_t)(cks >> 8);
    out[len++] = SHENGYI_FRAME_CR;
    out[len++] = SHENGYI_FRAME_LF;
    return len;
}

/* Validate a Shengyi frame header/checksum and return payload pointer. */
static inline int shengyi_frame_validate(const uint8_t *buf, size_t len, uint8_t cmd,
                                         uint8_t payload_len_min, const uint8_t **payload_out)
{
    if (!buf || len < 8 || buf[0] != SHENGYI_FRAME_START || buf[2] != cmd)
        return 0;
    uint8_t payload_len = buf[3];
    if (payload_len < payload_len_min)
        return 0;
    if ((size_t)payload_len + 8u > len)
        return 0;
    uint16_t expect = (uint16_t)(buf[len - 4] | ((uint16_t)buf[len - 3] << 8));
    uint16_t have = shengyi_checksum16(buf, len);
    if (expect != have)
        return 0;
    if (payload_out)
        *payload_out = &buf[4];
    return 1;
}

/* Build a 0x52 request frame into buffer.
 * Returns frame length, or 0 if buffer too small (need 14 bytes).
 */
static inline size_t shengyi_build_frame_0x52_req(uint8_t assist_level_mapped,
                                                  uint8_t headlight_enabled,
                                                  uint8_t walk_assist_active,
                                                  uint8_t speed_over_limit,
                                                  uint8_t *out,
                                                  size_t cap)
{
    if (!out || cap < 14)
        return 0;

    uint8_t flags = 0;
    if (headlight_enabled)
        flags |= 0x80u;
    if (walk_assist_active)
        flags |= 0x20u;
    if (speed_over_limit)
        flags |= 0x01u;

    uint8_t payload[2];
    payload[0] = assist_level_mapped;
    payload[1] = flags;
    return shengyi_frame_build(0x52u, payload, 2u, out, cap);
}

/* Build flags byte from current state */
uint8_t shengyi_build_flags(void);

/* Map virtual gear to OEM assist level byte */
uint8_t shengyi_assist_level_mapped(void);

/* OEM assist level helpers */
uint8_t shengyi_assist_oem_max(uint8_t count);
uint8_t shengyi_assist_oem_lut(uint8_t max, uint8_t idx);

#endif /* SHENGYI_H */
