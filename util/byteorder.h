#ifndef OPEN_FIRMWARE_UTIL_BYTEORDER_H
#define OPEN_FIRMWARE_UTIL_BYTEORDER_H

#include <stdint.h>

static inline void store_be16(uint8_t *dst, uint16_t v)
{
    dst[0] = (uint8_t)(v >> 8);
    dst[1] = (uint8_t)v;
}

static inline void store_be32(uint8_t *dst, uint32_t v)
{
    dst[0] = (uint8_t)(v >> 24);
    dst[1] = (uint8_t)(v >> 16);
    dst[2] = (uint8_t)(v >> 8);
    dst[3] = (uint8_t)v;
}

static inline uint16_t load_be16(const uint8_t *p)
{
    return (uint16_t)((p[0] << 8) | p[1]);
}

static inline uint32_t load_be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

#endif

