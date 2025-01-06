#include "util/crc32.h"

uint32_t crc32_update(uint32_t crc, const uint8_t *data, size_t len)
{
    if (!data || len == 0)
        return crc;
    for (size_t i = 0; i < len; ++i)
    {
        crc ^= (uint32_t)data[i];
        for (int j = 0; j < 8; ++j)
        {
            uint32_t mask = (uint32_t)(-(int32_t)(crc & 1u));
            crc = (crc >> 1) ^ (0xEDB88320u & mask);
        }
    }
    return crc;
}

uint32_t crc32_compute(const uint8_t *data, size_t len)
{
    return ~crc32_update(0xFFFFFFFFu, data, len);
}

