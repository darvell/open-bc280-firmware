#ifndef OPEN_FIRMWARE_DRIVERS_SPI_FLASH_H
#define OPEN_FIRMWARE_DRIVERS_SPI_FLASH_H

#include <stdint.h>

#define SPI_FLASH_SECTOR_SIZE   0x1000u
#define SPI_FLASH_PAGE_SIZE     256u

void spi_flash_read(uint32_t addr, uint8_t *out, uint32_t len);
void spi_flash_erase_4k(uint32_t addr);
void spi_flash_write(uint32_t addr, const uint8_t *data, uint32_t len);
void spi_flash_update_bytes(uint32_t addr, const uint8_t *data, uint32_t len);

/* OEM bootloader mode flag: if set, bootloader stays in BLE update mode. */
void spi_flash_set_bootloader_mode_flag(void);

#endif

