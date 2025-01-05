#include "storage/boot_stage.h"

#include "drivers/spi_flash.h"
#include "platform/time.h"
#include "storage/layout.h"
#include "util/byteorder.h"

#define BOOT_STAGE_ENTRY_SIZE  8u
#define BOOT_STAGE_ENTRY_COUNT (SPI_FLASH_SECTOR_SIZE / BOOT_STAGE_ENTRY_SIZE)

static uint8_t g_boot_stage_buf[SPI_FLASH_SECTOR_SIZE];
static uint16_t g_boot_stage_index;
static uint8_t g_boot_stage_inited;

static void boot_stage_scan(void)
{
    spi_flash_read(BOOT_STAGE_STORAGE_BASE, g_boot_stage_buf, SPI_FLASH_SECTOR_SIZE);
    for (uint16_t i = 0; i < BOOT_STAGE_ENTRY_COUNT; ++i)
    {
        const uint8_t *entry = &g_boot_stage_buf[i * BOOT_STAGE_ENTRY_SIZE];
        if (load_be32(entry) == 0xFFFFFFFFu)
        {
            g_boot_stage_index = i;
            return;
        }
    }
    g_boot_stage_index = BOOT_STAGE_ENTRY_COUNT;
}

void boot_stage_log(uint32_t code)
{
    if (!g_boot_stage_inited)
    {
        boot_stage_scan();
        g_boot_stage_inited = 1u;
    }

    if (g_boot_stage_index >= BOOT_STAGE_ENTRY_COUNT)
    {
        spi_flash_erase_4k(BOOT_STAGE_STORAGE_BASE);
        g_boot_stage_index = 0u;
    }

    uint8_t entry[BOOT_STAGE_ENTRY_SIZE];
    store_be32(&entry[0], code);
    store_be32(&entry[4], g_ms);

    spi_flash_write(BOOT_STAGE_STORAGE_BASE + ((uint32_t)g_boot_stage_index * BOOT_STAGE_ENTRY_SIZE),
                    entry, BOOT_STAGE_ENTRY_SIZE);
    g_boot_stage_index++;
}
