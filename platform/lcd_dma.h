#ifndef OPEN_FIRMWARE_PLATFORM_LCD_DMA_H
#define OPEN_FIRMWARE_PLATFORM_LCD_DMA_H

#include <stdint.h>

void platform_lcd_dma_write_u16(const uint16_t *pixels, uint32_t count);

#endif
