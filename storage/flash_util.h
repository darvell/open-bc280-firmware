#ifndef OPEN_FIRMWARE_STORAGE_FLASH_UTIL_H
#define OPEN_FIRMWARE_STORAGE_FLASH_UTIL_H

#include <stdint.h>

#include "drivers/spi_flash.h"

static inline void spi_flash_erase_region(uint32_t addr, uint32_t len)
{
    if (len == 0)
        return;
    uint32_t start = addr & ~(SPI_FLASH_SECTOR_SIZE - 1u);
    uint32_t end = (addr + len + (SPI_FLASH_SECTOR_SIZE - 1u)) & ~(SPI_FLASH_SECTOR_SIZE - 1u);
    for (uint32_t a = start; a < end; a += SPI_FLASH_SECTOR_SIZE)
        spi_flash_erase_4k(a);
}

#endif

