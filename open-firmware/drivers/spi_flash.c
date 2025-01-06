#include "drivers/spi_flash.h"

#include <stddef.h>

#include "platform/hw.h"
#include "platform/mmio.h"
#include "platform/time.h"

/* External SPI flash (W25Q32-class) is accessed over SPI1 with CS on PA4. */
#define SPI_FLASH_BOOTMODE_ADDR 0x003FF080u

static int spi_flash_hw_inited;

static inline void spi_flash_stage_mark(uint32_t value)
{
    (void)value;
}

static void spi_flash_hw_init_once(void)
{
    if (spi_flash_hw_inited)
        return;
    spi_flash_hw_inited = 1;

    /* Enable GPIOA + SPI1 clocks (APB2). */
    uint32_t apb2 = mmio_read32(RCC_APB2ENR);
    apb2 |= (1u << 2) | (1u << 12);
    mmio_write32(RCC_APB2ENR, apb2);

    /* Configure PA4 (CS) as output PP 50MHz, PA5/PA7 AF PP 50MHz, PA6 input pull-up. */
    uint32_t crl = mmio_read32(GPIOA_BASE + 0x00u);
    crl &= ~(
        (0xFu << (4u * 4u)) |
        (0xFu << (4u * 5u)) |
        (0xFu << (4u * 6u)) |
        (0xFu << (4u * 7u))
    );
    crl |= (0x3u << (4u * 4u)); /* PA4 output PP 50MHz */
    crl |= (0xBu << (4u * 5u)); /* PA5 AF PP 50MHz */
    crl |= (0x8u << (4u * 6u)); /* PA6 input pull-up/down */
    crl |= (0xBu << (4u * 7u)); /* PA7 AF PP 50MHz */
    mmio_write32(GPIOA_BASE + 0x00u, crl);

    /* Select pull-up for PA6, set CS high on PA4. */
    mmio_write32(GPIO_BSRR(GPIOA_BASE), (1u << 6) | (1u << 4));

    /* SPI1: master, software NSS, fPCLK/16, mode 0, 8-bit. */
    mmio_write32(SPI1_BASE + 0x04u, 0x00000000u); /* CR2 */
    uint32_t cr1 = 0;
    cr1 |= (1u << 2);  /* MSTR */
    cr1 |= (3u << 3);  /* BR[2:0] = /16 */
    cr1 |= (1u << 8);  /* SSI */
    cr1 |= (1u << 9);  /* SSM */
    cr1 |= (1u << 6);  /* SPE */
    mmio_write32(SPI1_BASE + 0x00u, cr1);
}

static inline void spi_flash_cs_low(void)
{
    /* STM32F1/AT32 style: BRR resets bits low. */
    mmio_write32(GPIO_BRR(GPIOA_BASE), (1u << 4)); /* PA4 low */
}

static inline void spi_flash_cs_high(void)
{
    /* STM32F1/AT32 style: BSRR sets bits high. */
    mmio_write32(GPIO_BSRR(GPIOA_BASE), (1u << 4)); /* PA4 high */
}

static uint8_t spi1_txrx_u8(uint8_t b)
{
    /* SR: RXNE bit0, TXE bit1 (matches OEM + Renode stub). */
    for (uint32_t i = 0; i < 500u; ++i)
    {
        if (mmio_read32(SPI1_BASE + 0x08u) & 0x2u) /* TXE */
            break;
    }
    mmio_write32(SPI1_BASE + 0x0Cu, b);
    for (uint32_t i = 0; i < 500u; ++i)
    {
        if (mmio_read32(SPI1_BASE + 0x08u) & 0x1u) /* RXNE */
            break;
    }
    return (uint8_t)mmio_read32(SPI1_BASE + 0x0Cu);
}

static void spi_flash_write_enable(void)
{
    spi_flash_cs_low();
    (void)spi1_txrx_u8(0x06u); /* WREN */
    spi_flash_cs_high();
}

static uint8_t spi_flash_read_sr1(void)
{
    spi_flash_cs_low();
    (void)spi1_txrx_u8(0x05u); /* RDSR */
    uint8_t v = spi1_txrx_u8(0x00u);
    spi_flash_cs_high();
    return v;
}

static void spi_flash_wait_ready(uint32_t timeout_ms)
{
    uint32_t start = g_ms;
    for (;;)
    {
        platform_time_poll_1ms();
        uint8_t sr = spi_flash_read_sr1();
        if ((sr & 0x01u) == 0u) /* WIP cleared */
            return;
        if (timeout_ms && (uint32_t)(g_ms - start) >= timeout_ms)
            return;
    }
}

static void spi_flash_page_program(uint32_t addr, const uint8_t *data, uint32_t len)
{
    if (!data || len == 0 || len > SPI_FLASH_PAGE_SIZE)
        return;
    spi_flash_write_enable();
    spi_flash_cs_low();
    (void)spi1_txrx_u8(0x02u); /* PP */
    (void)spi1_txrx_u8((uint8_t)(addr >> 16));
    (void)spi1_txrx_u8((uint8_t)(addr >> 8));
    (void)spi1_txrx_u8((uint8_t)(addr));
    for (uint32_t i = 0; i < len; ++i)
        (void)spi1_txrx_u8(data[i]);
    spi_flash_cs_high();
    spi_flash_wait_ready(2000u);
}

void spi_flash_read(uint32_t addr, uint8_t *out, uint32_t len)
{
    if (!out || len == 0)
        return;
    spi_flash_hw_init_once();
    spi_flash_cs_low();
    (void)spi1_txrx_u8(0x03u); /* READ */
    (void)spi1_txrx_u8((uint8_t)(addr >> 16));
    (void)spi1_txrx_u8((uint8_t)(addr >> 8));
    (void)spi1_txrx_u8((uint8_t)(addr));
    for (uint32_t i = 0; i < len; ++i)
        out[i] = spi1_txrx_u8(0x00u);
    spi_flash_cs_high();
}

void spi_flash_erase_4k(uint32_t addr)
{
    spi_flash_hw_init_once();
    uint32_t sector = addr & ~(SPI_FLASH_SECTOR_SIZE - 1u);
    spi_flash_write_enable();
    spi_flash_cs_low();
    (void)spi1_txrx_u8(0x20u); /* SE (4K) */
    (void)spi1_txrx_u8((uint8_t)(sector >> 16));
    (void)spi1_txrx_u8((uint8_t)(sector >> 8));
    (void)spi1_txrx_u8((uint8_t)(sector));
    spi_flash_cs_high();
    spi_flash_wait_ready(2000u);
}

void spi_flash_write(uint32_t addr, const uint8_t *data, uint32_t len)
{
    if (!data || len == 0)
        return;
    spi_flash_hw_init_once();

    uint32_t cur = addr;
    uint32_t remaining = len;
    const uint8_t *p = data;
    while (remaining)
    {
        uint32_t page_off = cur & (SPI_FLASH_PAGE_SIZE - 1u);
        uint32_t chunk = SPI_FLASH_PAGE_SIZE - page_off;
        if (chunk > remaining)
            chunk = remaining;
        spi_flash_page_program(cur, p, chunk);
        cur += chunk;
        p += chunk;
        remaining -= chunk;
    }
}

static uint8_t g_spi_flash_sector_buf[SPI_FLASH_SECTOR_SIZE];

void spi_flash_update_bytes(uint32_t addr, const uint8_t *data, uint32_t len)
{
    if (!data || len == 0)
        return;

    uint32_t cur = addr;
    uint32_t remaining = len;
    const uint8_t *p = data;

    while (remaining)
    {
        uint32_t sector = cur & ~(SPI_FLASH_SECTOR_SIZE - 1u);
        uint32_t off = cur - sector;
        uint32_t chunk = SPI_FLASH_SECTOR_SIZE - off;
        if (chunk > remaining)
            chunk = remaining;

        spi_flash_read(sector, g_spi_flash_sector_buf, SPI_FLASH_SECTOR_SIZE);
        for (uint32_t i = 0; i < chunk; ++i)
            g_spi_flash_sector_buf[off + i] = p[i];

        spi_flash_erase_4k(sector);
        for (uint32_t page = 0; page < SPI_FLASH_SECTOR_SIZE; page += SPI_FLASH_PAGE_SIZE)
            spi_flash_page_program(sector + page, &g_spi_flash_sector_buf[page], SPI_FLASH_PAGE_SIZE);

        cur += chunk;
        p += chunk;
        remaining -= chunk;
    }
}

void spi_flash_set_bootloader_mode_flag(void)
{
    spi_flash_stage_mark(0xB200);
    spi_flash_hw_init_once();
    /*
     * Conservative update: patch the containing 4KB sector and rewrite it, so we
     * don't accidentally destroy nearby OEM metadata (e.g. bootloader tag data).
     */
    uint32_t sector = SPI_FLASH_BOOTMODE_ADDR & ~(SPI_FLASH_SECTOR_SIZE - 1u);
    spi_flash_read(sector, g_spi_flash_sector_buf, SPI_FLASH_SECTOR_SIZE);
    spi_flash_stage_mark(0xB201);

    uint32_t off = SPI_FLASH_BOOTMODE_ADDR - sector;
    if (off + 4u <= SPI_FLASH_SECTOR_SIZE)
    {
        g_spi_flash_sector_buf[off + 0u] = 0xAA;
        g_spi_flash_sector_buf[off + 1u] = 0x00;
        g_spi_flash_sector_buf[off + 2u] = 0x00;
        g_spi_flash_sector_buf[off + 3u] = 0x00;
    }
    spi_flash_stage_mark(0xB202);

    spi_flash_erase_4k(sector);
    spi_flash_stage_mark(0xB203);
    for (uint32_t page = 0; page < SPI_FLASH_SECTOR_SIZE; page += SPI_FLASH_PAGE_SIZE)
        spi_flash_page_program(sector + page, &g_spi_flash_sector_buf[page], SPI_FLASH_PAGE_SIZE);
    spi_flash_stage_mark(0xB204);
}
