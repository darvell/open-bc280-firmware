#include "ble_hacker.h"

static void copy_bytes(uint8_t *dst, const uint8_t *src, uint8_t len)
{
    if (!dst || !src)
        return;
    for (uint8_t i = 0; i < len; ++i)
        dst[i] = src[i];
}

int ble_hacker_decode(const uint8_t *buf, uint8_t len,
                      ble_hacker_frame_t *out, uint8_t *status)
{
    if (!buf || len < 3u)
    {
        if (status)
            *status = BLE_HACKER_STATUS_BAD_LENGTH;
        return 0;
    }

    uint8_t ver = buf[0];
    uint8_t op = buf[1];
    uint8_t plen = buf[2];

    if (ver != BLE_HACKER_VERSION)
    {
        if (status)
            *status = BLE_HACKER_STATUS_BAD_VERSION;
        return 0;
    }

    if ((uint8_t)(plen + 3u) != len)
    {
        if (status)
            *status = BLE_HACKER_STATUS_BAD_LENGTH;
        return 0;
    }

    if (out)
    {
        out->version = ver;
        out->opcode = op;
        out->payload_len = plen;
        out->payload = &buf[3];
    }

    if (status)
        *status = BLE_HACKER_STATUS_OK;
    return 1;
}

uint8_t ble_hacker_encode(uint8_t opcode, const uint8_t *payload,
                          uint8_t payload_len, uint8_t *out, uint8_t out_max)
{
    uint8_t total = (uint8_t)(payload_len + 3u);
    if (!out || out_max < total)
        return 0;

    out[0] = BLE_HACKER_VERSION;
    out[1] = opcode;
    out[2] = payload_len;
    if (payload_len && payload)
        copy_bytes(&out[3], payload, payload_len);
    return total;
}

uint8_t ble_hacker_encode_status(uint8_t opcode, uint8_t status,
                                 const uint8_t *payload, uint8_t payload_len,
                                 uint8_t *out, uint8_t out_max)
{
    uint8_t total_payload = (uint8_t)(payload_len + 1u);
    uint8_t total = (uint8_t)(total_payload + 3u);
    if (!out || out_max < total)
        return 0;

    out[0] = BLE_HACKER_VERSION;
    out[1] = opcode;
    out[2] = total_payload;
    out[3] = status;
    if (payload_len && payload)
        copy_bytes(&out[4], payload, payload_len);
    return total;
}
