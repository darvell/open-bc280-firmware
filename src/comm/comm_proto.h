#ifndef COMM_PROTO_H
#define COMM_PROTO_H

#include <stddef.h>
#include <stdint.h>

/* Protocol constants */
#define COMM_SOF         0x55
#define COMM_MAX_PAYLOAD 192

/* XOR checksum (inverted) for 0x55-framed protocol data. */
static inline uint8_t checksum(const uint8_t *buf, size_t len)
{
    uint8_t sum = 0;
    for (size_t i = 0; i < len; ++i)
        sum ^= buf[i];
    return (uint8_t)(~sum);
}

static inline size_t comm_frame_build(uint8_t *out, size_t cap, uint8_t cmd,
                                      const uint8_t *payload, uint8_t len)
{
    if (!out)
        return 0;
    if (len > COMM_MAX_PAYLOAD)
        return 0;
    if (len && !payload)
        return 0;
    size_t total = (size_t)len + 4u;
    if (cap < total)
        return 0;
    out[0] = COMM_SOF;
    out[1] = cmd;
    out[2] = len;
    for (uint8_t i = 0; i < len; ++i)
        out[3 + i] = payload[i];
    out[3 + len] = checksum(out, 3 + len);
    return total;
}

static inline int comm_frame_validate(const uint8_t *frame, size_t len, uint8_t *expected_out)
{
    if (!frame || len < 4)
    {
        if (expected_out)
            *expected_out = 0;
        return 0;
    }
    if (frame[0] != COMM_SOF)
    {
        if (expected_out)
            *expected_out = 0;
        return 0;
    }
    uint8_t payload_len = frame[2];
    if (payload_len > COMM_MAX_PAYLOAD)
    {
        if (expected_out)
            *expected_out = 0;
        return 0;
    }
    if (len != (size_t)(payload_len + 4u))
    {
        if (expected_out)
            *expected_out = 0;
        return 0;
    }
    uint8_t expected = checksum(frame, len - 1);
    if (expected_out)
        *expected_out = expected;
    return expected == frame[len - 1];
}

static inline int comm_frame_is_valid(const uint8_t *frame, size_t len)
{
    return comm_frame_validate(frame, len, NULL);
}

typedef enum {
    COMM_PARSE_NONE = 0,
    COMM_PARSE_FRAME = 1,
    COMM_PARSE_ERROR = 2
} comm_parse_result_t;

/* Fixed-size streaming telemetry payload (v1). */
#define COMM_STATE_FRAME_V1_LEN 22u

typedef struct {
    uint32_t ms;
    uint16_t speed_dmph;
    uint16_t cadence_rpm;
    uint16_t power_w;
    int16_t batt_dV;
    int16_t batt_dA;
    int16_t ctrl_temp_dC;
    uint8_t assist_mode;
    uint8_t profile_id;
    uint8_t virtual_gear;
    uint8_t flags;
} comm_state_frame_t;

static inline uint8_t comm_state_frame_build_v1(uint8_t *out, uint8_t max_len, const comm_state_frame_t *state)
{
    if (!out || !state || max_len < COMM_STATE_FRAME_V1_LEN)
        return 0;
    out[0] = 1; /* version */
    out[1] = COMM_STATE_FRAME_V1_LEN;
    out[2] = (state->ms >> 24) & 0xFF;
    out[3] = (state->ms >> 16) & 0xFF;
    out[4] = (state->ms >> 8) & 0xFF;
    out[5] = state->ms & 0xFF;
    out[6] = state->speed_dmph >> 8;
    out[7] = state->speed_dmph & 0xFF;
    out[8] = state->cadence_rpm >> 8;
    out[9] = state->cadence_rpm & 0xFF;
    out[10] = state->power_w >> 8;
    out[11] = state->power_w & 0xFF;
    out[12] = (uint16_t)state->batt_dV >> 8;
    out[13] = (uint16_t)state->batt_dV & 0xFF;
    out[14] = (uint16_t)state->batt_dA >> 8;
    out[15] = (uint16_t)state->batt_dA & 0xFF;
    out[16] = (uint16_t)state->ctrl_temp_dC >> 8;
    out[17] = (uint16_t)state->ctrl_temp_dC & 0xFF;
    out[18] = state->assist_mode;
    out[19] = state->profile_id;
    out[20] = state->virtual_gear;
    out[21] = state->flags;
    return COMM_STATE_FRAME_V1_LEN;
}

/* Incremental frame parser (framing only). */
static inline comm_parse_result_t comm_parser_feed(uint8_t *buf, size_t cap, uint8_t max_payload,
                                                   uint8_t *len_io, uint8_t byte,
                                                   uint8_t *out_frame_len)
{
    if (!buf || !len_io || cap < 4u)
    {
        if (out_frame_len)
            *out_frame_len = 0;
        return COMM_PARSE_ERROR;
    }
    if (*len_io == 0u)
    {
        if (byte != COMM_SOF)
            return COMM_PARSE_NONE;
        buf[0] = byte;
        *len_io = 1u;
        return COMM_PARSE_NONE;
    }
    if (*len_io >= cap)
    {
        *len_io = 0u;
        if (out_frame_len)
            *out_frame_len = 0;
        return COMM_PARSE_ERROR;
    }

    buf[*len_io] = byte;
    (*len_io)++;

    if (*len_io == 3u)
    {
        uint8_t payload_len = buf[2];
        if (payload_len > max_payload || (size_t)payload_len + 4u > cap)
        {
            *len_io = 0u;
            if (out_frame_len)
                *out_frame_len = 0;
            return COMM_PARSE_ERROR;
        }
    }

    if (*len_io >= 3u)
    {
        uint8_t payload_len = buf[2];
        size_t total = (size_t)payload_len + 4u;
        if (total > cap)
        {
            *len_io = 0u;
            if (out_frame_len)
                *out_frame_len = 0;
            return COMM_PARSE_ERROR;
        }
        if (*len_io == total)
        {
            if (out_frame_len)
                *out_frame_len = (uint8_t)total;
            *len_io = 0u;
            return COMM_PARSE_FRAME;
        }
        if (*len_io > total)
        {
            *len_io = 0u;
            if (out_frame_len)
                *out_frame_len = 0;
            return COMM_PARSE_ERROR;
        }
    }
    return COMM_PARSE_NONE;
}

#endif /* COMM_PROTO_H */
