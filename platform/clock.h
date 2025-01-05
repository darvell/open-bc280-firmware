#ifndef OPEN_FIRMWARE_PLATFORM_CLOCK_H
#define OPEN_FIRMWARE_PLATFORM_CLOCK_H

#include <stdint.h>

uint32_t rcc_get_hclk_hz_fallback(void);
uint32_t rcc_get_pclk_hz_fallback(uint8_t apb2);
void platform_clock_init(void);

#endif
