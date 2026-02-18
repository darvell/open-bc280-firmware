#include "motor_stx02.h"
#include "../util/bool_to_u8.h"
#include <stdint.h>

/* Frame layout constants for STX02 (OEM mode=1) packets. */
#define STX02_SOF_BYTE                0x02u
#define STX02_MIN_PAYLOAD_LEN         10u
#define STX02_CMD1_ID                 1u
#define STX02_HEADER_BYTES            3u
#define STX02_CHECKSUM_BYTES          1u
#define STX02_MIN_FRAME_BYTES         (STX02_HEADER_BYTES + STX02_MIN_PAYLOAD_LEN + STX02_CHECKSUM_BYTES)

static uint8_t stx02_err_from_flags(uint8_t flags)
{
    /*
     * OEM v2.5.1 maps a priority-ordered error code from the cmd==1 flags byte.
     * Evidence: APP_process_motor_response_packet @ 0x08021CA8:
     * - bit1 => 2
     * - bit3 => 6
     * - bit0 => 7
     * - bit5 => 8
     * - bit4 => 9
     * - bit6 => 20
     * - else => 0
     */
    if (flags & (1u << 1)) return 2u;
    if (flags & (1u << 3)) return 6u;
    if (flags & (1u << 0)) return 7u;
    if (flags & (1u << 5)) return 8u;
    if (flags & (1u << 4)) return 9u;
    if (flags & (1u << 6)) return 20u;
    return 0u;
}

bool motor_stx02_decode_cmd1(const uint8_t *frame, uint8_t len, motor_stx02_cmd1_t *out)
{
    if (!frame || !out)
        return false;

    /* Minimum: SOF + LEN + CMD + payload(10) + XOR. */
    if (len < STX02_MIN_FRAME_BYTES)
        return false;
    if (frame[0] != STX02_SOF_BYTE)
        return false;

    /*
     * OEM uses LEN as the total captured frame length (including XOR byte).
     * Our ISR typically captures exactly LEN bytes, but accept larger buffers.
     */
    uint8_t exp = frame[1];
    if (exp < STX02_MIN_FRAME_BYTES || exp > len)
        return false;

    if (frame[2] != STX02_CMD1_ID)
        return false;

    const uint8_t *p = &frame[3];
    uint8_t flags = p[0];

    /* payload[2..3] big-endian, top bits are flags. */
    uint16_t raw = (uint16_t)((uint16_t)p[2] << 8) | p[3];
    uint16_t val14 = (uint16_t)(raw & 0x3FFFu);
    bool scale_deci = (raw & 0x4000u) != 0u;

    /*
     * OEM scaling:
     * - if 0x4000 set: (val * 0.1) * 1000 mA => val * 100 mA => val dA
     * - else: val * 1000 mA => (val * 10) dA
     */
    uint32_t dA = scale_deci ? (uint32_t)val14 : ((uint32_t)val14 * 10u);
    if (dA > 32767u)
        dA = 32767u;

    out->flags = flags;
    out->err_code = stx02_err_from_flags(flags);
    out->flag_bit2 = (uint8_t)((flags >> 2) & 1u);
    out->flag_bit7 = (uint8_t)((flags >> 7) & 1u);
    out->current_dA = (int16_t)dA;
    out->period_ms = (uint16_t)((uint16_t)p[5] << 8) | p[6];

    out->soc_pct = p[7];
    out->soc_valid = bool_to_u8(out->soc_pct <= 100u);

    return true;
}
