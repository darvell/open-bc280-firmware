#ifndef UI_COLOR_H
#define UI_COLOR_H

#include <stdint.h>

static inline uint16_t rgb565_dim(uint16_t c)
{
    /* Common RGB565 dim: halves brightness with minimal artifacts. */
    return (uint16_t)((c >> 1) & 0x7BEFu);
}

#endif
