#pragma once

#include <stdint.h>

static inline uint8_t bool_to_u8(uint32_t condition)
{
    return (uint8_t)((condition != 0u) ? 1u : 0u);
}
