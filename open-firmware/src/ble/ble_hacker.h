#pragma once

#include <stdint.h>

#define BLE_HACKER_VERSION 1u
#define BLE_HACKER_OP_RESPONSE_FLAG 0x80u
#define BLE_HACKER_OP_ERROR 0x7Fu

#define BLE_HACKER_STATUS_OK          0x00u
#define BLE_HACKER_STATUS_BAD_VERSION 0xF0u
#define BLE_HACKER_STATUS_BAD_LENGTH  0xF1u
#define BLE_HACKER_STATUS_BAD_PAYLOAD 0xF2u
#define BLE_HACKER_STATUS_BAD_OPCODE  0xF3u
#define BLE_HACKER_STATUS_BLOCKED     0xF4u

#define BLE_HACKER_OP_VERSION      0x01u
#define BLE_HACKER_OP_TELEMETRY    0x02u
#define BLE_HACKER_OP_CONFIG_GET   0x10u
#define BLE_HACKER_OP_CONFIG_STAGE 0x11u
#define BLE_HACKER_OP_CONFIG_COMMIT 0x12u
#define BLE_HACKER_OP_DEBUG_LINE   0x20u

#define BLE_HACKER_CAP_TELEMETRY 0x01u
#define BLE_HACKER_CAP_CONFIG    0x02u
#define BLE_HACKER_CAP_DEBUG     0x04u

typedef struct {
    uint8_t version;
    uint8_t opcode;
    uint8_t payload_len;
    const uint8_t *payload;
} ble_hacker_frame_t;

int ble_hacker_decode(const uint8_t *buf, uint8_t len,
                      ble_hacker_frame_t *out, uint8_t *status);

uint8_t ble_hacker_encode(uint8_t opcode, const uint8_t *payload,
                          uint8_t payload_len, uint8_t *out, uint8_t out_max);

uint8_t ble_hacker_encode_status(uint8_t opcode, uint8_t status,
                                 const uint8_t *payload, uint8_t payload_len,
                                 uint8_t *out, uint8_t out_max);
