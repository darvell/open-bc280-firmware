#ifndef OPEN_FIRMWARE_MOTOR_STX02_H
#define OPEN_FIRMWARE_MOTOR_STX02_H

#include <stdbool.h>
#include <stdint.h>

/*
 * Protocol B (v2.5.1): 0x02 SOF, LEN, CMD, payload..., XOR
 *
 * This module implements conservative, evidence-backed decoding for the
 * motor->display status packet with cmd==1 (10-byte payload) as observed in
 * the OEM BC280 app v2.5.1.
 *
 * Notes:
 * - Do not infer "control" semantics here. Only decode telemetry and flags.
 * - OEM function anchor for cmd==1 payload handling: 0x08021CA8.
 */

typedef struct motor_stx02_cmd1_t {
    uint8_t flags;       /* payload[0] */
    uint8_t err_code;    /* derived from flags bits (OEM priority mapping) */

    /* OEM stores these bits in app state; semantics are still variant-dependent. */
    uint8_t flag_bit2;   /* (flags >> 2) & 1 */
    uint8_t flag_bit7;   /* (flags >> 7) & 1 */

    int16_t current_dA;  /* payload[2..3] decoded to 0.1 A units, non-negative */
    uint16_t period_ms;  /* payload[5..6] big-endian: wheel period in ms/rev */

    uint8_t soc_pct;     /* payload[7] when valid */
    uint8_t soc_valid;   /* 1 if soc_pct looks like a percent (<=100) */
} motor_stx02_cmd1_t;

/* Returns true if frame is a valid cmd==1 STX02 packet and out is filled. */
bool motor_stx02_decode_cmd1(const uint8_t *frame, uint8_t len, motor_stx02_cmd1_t *out);

#endif
