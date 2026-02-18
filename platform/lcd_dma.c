#include "platform/lcd_dma.h"

#include "platform/hw.h"
#include "platform/mmio.h"

void platform_lcd_dma_write_u16(const uint16_t *pixels, uint32_t count)
{
    if (!pixels || count == 0u)
        return;

    /* OEM app writes LCD over FSMC without DMA; keep the same behavior. */
    volatile uint16_t *dst = (volatile uint16_t *)LCD_DATA_ADDR;
    for (uint32_t i = 0; i < count; ++i)
        *dst = pixels[i];
}
