#ifndef OPEN_FIRMWARE_UTIL_CRC32_H
#define OPEN_FIRMWARE_UTIL_CRC32_H

#include <stddef.h>
#include <stdint.h>

/* CRC32 polynomial 0xEDB88320, seed 0xFFFFFFFF, final ~ (Ethernet/PKZip). */
uint32_t crc32_update(uint32_t crc, const uint8_t *data, size_t len);
uint32_t crc32_compute(const uint8_t *data, size_t len);

#endif

