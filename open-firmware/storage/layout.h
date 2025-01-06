#ifndef OPEN_FIRMWARE_STORAGE_LAYOUT_H
#define OPEN_FIRMWARE_STORAGE_LAYOUT_H

#include <stdint.h>

#include "drivers/spi_flash.h"

/* Partition base inside external SPI flash (offset, not MMIO). */
#define SPI_FLASH_STORAGE_BASE  0x00300000u

/* Config blob (double-buffered). */
#define CONFIG_SLOT_COUNT 2
#define CONFIG_SLOT_STRIDE SPI_FLASH_SECTOR_SIZE
#define CONFIG_STORAGE_BASE (SPI_FLASH_STORAGE_BASE + 0x0008000u) /* keep clear of bootloader flag near 0x3FF080 */

/* Event log (flash-backed, erase-on-wrap). */
#define EVENT_LOG_STORAGE_BASE (CONFIG_STORAGE_BASE + 0x0003000u)
#define EVENT_LOG_STORAGE_BYTES 0x0002000u /* 2x 4KB sectors (includes slack) */

/* Stream log (flash-backed, erase-on-wrap). */
#define STREAM_LOG_STORAGE_BASE (CONFIG_STORAGE_BASE + 0x0006000u)
#define STREAM_LOG_STORAGE_BYTES 0x0003000u /* 3x 4KB sectors (includes slack) */

/* Trip summary (last ride snapshot). */
#define TRIP_STORAGE_BASE (CONFIG_STORAGE_BASE + 0x0002000u)

/* Crash dump snapshot. */
#define CRASH_DUMP_STORAGE_BASE (SPI_FLASH_STORAGE_BASE + 0x00011000u)

/* A/B update metadata + image slots (flash staging). */
#define AB_META_BASE (SPI_FLASH_STORAGE_BASE + 0x00012000u)
#define AB_SLOT0_BASE (SPI_FLASH_STORAGE_BASE + 0x00014000u)
#define AB_SLOT_STRIDE 0x00040000u
#define AB_SLOT1_BASE (AB_SLOT0_BASE + AB_SLOT_STRIDE)

#endif

