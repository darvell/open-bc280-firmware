#include "drivers/spi_flash.h"

#include <stddef.h>

#include "platform/hw.h"
#include "platform/irq_dma.h"
#include "platform/mmio.h"
#include "platform/time.h"
#include "storage/layout.h"

/* External SPI flash (W25Q32-class) is accessed over SPI1 with CS on PA4. */
#define DMA1_BASE 0x40020000u
#define DMA1_IFCR (DMA1_BASE + 0x04u)
#define DMA1_CH2_BASE (DMA1_BASE + 0x1Cu)
#define DMA1_CH3_BASE (DMA1_BASE + 0x30u)
#define DMA_CCR(ch) ((ch) + 0x00u)
#define DMA_CNDTR(ch) ((ch) + 0x04u)
#define DMA_CPAR(ch) ((ch) + 0x08u)
#define DMA_CMAR(ch) ((ch) + 0x0Cu)

static int spi_flash_hw_inited;
static uint8_t g_spi_dma_stub_rx[4] __attribute__((aligned(4)));
static uint8_t g_spi_dma_stub_tx[4] __attribute__((aligned(4)));

static inline void spi1_disable(void)
{
    mmio_write32(SPI1_BASE + 0x00u, mmio_read32(SPI1_BASE + 0x00u) & ~0x40u);
}

static inline void spi1_enable(void)
{
    mmio_write32(SPI1_BASE + 0x00u, mmio_read32(SPI1_BASE + 0x00u) | 0x40u);
}

static void spi1_apply_cr1(uint32_t extra_bits)
{
    uint32_t cr1 = mmio_read32(SPI1_BASE + 0x00u);
    cr1 = (cr1 & 0x3000u) | 0x030Cu | (extra_bits & 0x0C00u);
    mmio_write32(SPI1_BASE + 0x00u, cr1);
}

static inline void spi_flash_stage_mark(uint32_t value)
{
    (void)value;
}

static void nvic_set_priority(uint8_t irq, uint8_t priority)
{
    uint32_t addr = NVIC_IPR_BASE + irq;
    uint32_t word = addr & ~0x3u;
    uint32_t shift = (addr & 0x3u) * 8u;
    uint32_t v = mmio_read32(word);
    v = (v & ~(0xFFu << shift)) | ((uint32_t)priority << shift);
    mmio_write32(word, v);
}

static void spi_flash_gpio_configure_mask(uint32_t base, uint16_t mask, uint8_t mode_byte, uint8_t extend)
{
    uint8_t mode = (uint8_t)(mode_byte & 0x0Fu);
    if (mode_byte & 0x10u)
        mode = (uint8_t)(mode | (extend & 0x0Fu));

    uint32_t crl = mmio_read32(GPIO_CRL(base));
    uint32_t crh = mmio_read32(GPIO_CRH(base));

    for (uint8_t pin = 0; pin < 8u; ++pin)
    {
        if (mask & (1u << pin))
        {
            uint32_t shift = (uint32_t)pin * 4u;
            crl = (crl & ~(0xFu << shift)) | ((uint32_t)mode << shift);
        }
    }

    for (uint8_t pin = 8u; pin < 16u; ++pin)
    {
        if (mask & (1u << pin))
        {
            uint32_t shift = (uint32_t)(pin - 8u) * 4u;
            crh = (crh & ~(0xFu << shift)) | ((uint32_t)mode << shift);
        }
    }

    mmio_write32(GPIO_CRL(base), crl);
    mmio_write32(GPIO_CRH(base), crh);

    if (mode_byte == 0x28u)
        mmio_write32(GPIO_BRR(base), mask);
    else if (mode_byte == 0x48u)
        mmio_write32(GPIO_BSRR(base), mask);
}

static void spi_flash_hw_init_once(void)
{
    if (spi_flash_hw_inited)
        return;
    spi_flash_hw_inited = 1;

    /* OEM-style enable: GPIOA + SPI1 on APB2. */
    uint32_t apb2 = mmio_read32(RCC_APB2ENR);
    apb2 |= (1u << 2) | (1u << 12);
    mmio_write32(RCC_APB2ENR, apb2);

    /* OEM-style SPI1 reset toggle. */
    mmio_write32(RCC_APB2RSTR, mmio_read32(RCC_APB2RSTR) | (1u << 12));
    mmio_write32(RCC_APB2RSTR, mmio_read32(RCC_APB2RSTR) & ~(1u << 12));

    /* OEM-style GPIO config: PA5/PA7 (0x18), PA6 (0x48), PA4 (0x10). */
    spi_flash_gpio_configure_mask(GPIOA_BASE, 0x00A0u, 0x18u, 0x02u);
    /* Pull-up input: extend must be 0 so CRL/CRH nibble stays 0x8 (input pull-up/down). */
    spi_flash_gpio_configure_mask(GPIOA_BASE, 0x0040u, 0x48u, 0x00u);
    spi_flash_gpio_configure_mask(GPIOA_BASE, 0x0010u, 0x10u, 0x02u);
    mmio_write32(GPIO_BSRR(GPIOA_BASE), (1u << 4)); /* CS high */

    /* OEM-style SPI1 init: CR1 = 0x030C (MSTR + BR=/4 + SSI/SSM), SPE enabled later. */
    uint32_t cr1 = mmio_read32(SPI1_BASE + 0x00u);
    cr1 = (cr1 & 0x3040u) | 0x030Cu;
    mmio_write32(SPI1_BASE + 0x00u, cr1);

    uint32_t cr2 = mmio_read32(SPI1_BASE + 0x04u);
    cr2 &= ~0x0100u;
    mmio_write32(SPI1_BASE + 0x04u, cr2);

    uint32_t i2scfgr = mmio_read32(SPI1_BASE + 0x1Cu);
    i2scfgr &= ~0x0800u;
    mmio_write32(SPI1_BASE + 0x1Cu, i2scfgr);

    mmio_write32(SPI1_BASE + 0x10u, 0x0007u); /* CRCPR */

    cr1 = mmio_read32(SPI1_BASE + 0x00u);
    cr1 |= 0x0040u; /* SPE */
    mmio_write32(SPI1_BASE + 0x00u, cr1);

    /* OEM-style DMA channel reset/flag clear (no IRQ enable). */
    mmio_write32(RCC_AHBENR, mmio_read32(RCC_AHBENR) | (1u << 0));
    mmio_write32(DMA_CCR(DMA1_CH2_BASE), mmio_read32(DMA_CCR(DMA1_CH2_BASE)) & ~1u);
    mmio_write32(DMA_CCR(DMA1_CH3_BASE), mmio_read32(DMA_CCR(DMA1_CH3_BASE)) & ~1u);
    mmio_write32(DMA_CNDTR(DMA1_CH2_BASE), 0u);
    mmio_write32(DMA_CPAR(DMA1_CH2_BASE), 0u);
    mmio_write32(DMA_CMAR(DMA1_CH2_BASE), 0u);
    mmio_write32(DMA_CNDTR(DMA1_CH3_BASE), 0u);
    mmio_write32(DMA_CPAR(DMA1_CH3_BASE), 0u);
    mmio_write32(DMA_CMAR(DMA1_CH3_BASE), 0u);
    mmio_write32(DMA1_IFCR, (0x0Fu << 4) | (0x0Fu << 8));

    /* OEM-style: enable DMA1 CH2/CH3 NVIC lines (12/13). */
    /* Priorities match OEM app 2.2.5 with AIRCR PRIGROUP=0x5 (SCB_AIRCR=0x500). */
    nvic_set_priority(12u, 0x80u);
    nvic_set_priority(13u, 0x40u);
    mmio_write32(NVIC_ISER0, (1u << 12) | (1u << 13));

    /* OEM-style DMA channel presets for SPI1 DR (channels 2/3). */
    mmio_write32(DMA_CCR(DMA1_CH2_BASE), (mmio_read32(DMA_CCR(DMA1_CH2_BASE)) & ~0x7FF0u) | 0x3500u);
    mmio_write32(DMA_CPAR(DMA1_CH2_BASE), SPI1_BASE + 0x0Cu);
    mmio_write32(DMA_CMAR(DMA1_CH2_BASE), (uint32_t)g_spi_dma_stub_rx);
    mmio_write32(DMA_CNDTR(DMA1_CH2_BASE), 0u);

    mmio_write32(DMA_CCR(DMA1_CH3_BASE), (mmio_read32(DMA_CCR(DMA1_CH3_BASE)) & ~0x7FF0u) | 0x1510u);
    mmio_write32(DMA_CPAR(DMA1_CH3_BASE), SPI1_BASE + 0x0Cu);
    mmio_write32(DMA_CMAR(DMA1_CH3_BASE), (uint32_t)g_spi_dma_stub_tx);
    mmio_write32(DMA_CNDTR(DMA1_CH3_BASE), 0u);

    /* OEM-style: enable SPI DMA requests (CR2 bits 0/1). */
    uint32_t cr2_spi = mmio_read32(SPI1_BASE + 0x04u);
    cr2_spi |= 0x0003u;
    mmio_write32(SPI1_BASE + 0x04u, cr2_spi);
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
    /* SR: RXNE bit0, TXE bit1 (matches OEM + simulator stub). */
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

static uint8_t spi1_txrx_u8_oem(uint8_t b)
{
    /* Match OEM: timeout once counter exceeds 0xC8. */
    uint32_t t = 0;
    while ((mmio_read32(SPI1_BASE + 0x08u) & 0x2u) == 0u) /* TXE */
    {
        if (t++ > 0xC8u)
            return 0u;
    }
    mmio_write32(SPI1_BASE + 0x0Cu, b);

    t = 0;
    while ((mmio_read32(SPI1_BASE + 0x08u) & 0x1u) == 0u) /* RXNE */
    {
        if (t++ > 0xC8u)
            return 0u;
    }
    return (uint8_t)mmio_read32(SPI1_BASE + 0x0Cu);
}

static void spi_flash_dma_to_lcd(uint32_t addr, uint32_t lcd_addr, uint16_t count)
{
    if (count == 0u)
        return;

    spi_flash_hw_init_once();
    g_spi_dma_rx_done = 0u;

    spi1_disable();
    mmio_write32(DMA_CCR(DMA1_CH2_BASE), mmio_read32(DMA_CCR(DMA1_CH2_BASE)) & ~1u);

    /* Command phase: 8-bit, no RXONLY/DFF. */
    spi1_apply_cr1(0u);

    /* Program DMA RX channel (CH2) for LCD write. */
    mmio_write32(DMA_CMAR(DMA1_CH2_BASE), lcd_addr);
    mmio_write32(DMA_CNDTR(DMA1_CH2_BASE), count);

    uint32_t ccr = (mmio_read32(DMA_CCR(DMA1_CH2_BASE)) & 0xFFFF800Fu);
    ccr |= 0x3500u; /* 16-bit sizes + high priority */
    mmio_write32(DMA_CCR(DMA1_CH2_BASE), ccr);

    spi1_enable();
    spi_flash_cs_low();

    (void)spi1_txrx_u8_oem(0x03u);
    (void)spi1_txrx_u8_oem((uint8_t)(addr >> 16));
    (void)spi1_txrx_u8_oem((uint8_t)(addr >> 8));
    (void)spi1_txrx_u8_oem((uint8_t)(addr));
    (void)mmio_read32(SPI1_BASE + 0x0Cu); /* Clear RXNE. */

    spi1_disable();
    spi1_apply_cr1(0x0C00u); /* RXONLY + DFF (16-bit) */

    /* Clear DMA1 CH2 GIF (OEM uses 0x10). */
    mmio_write32(DMA1_IFCR, 0x10u);

    /* Enable TCIE then enable CH2. */
    mmio_write32(DMA_CCR(DMA1_CH2_BASE), mmio_read32(DMA_CCR(DMA1_CH2_BASE)) | 0x2u);
    spi1_enable();
    mmio_write32(DMA_CCR(DMA1_CH2_BASE), mmio_read32(DMA_CCR(DMA1_CH2_BASE)) | 1u);

    while (!g_spi_dma_rx_done)
        ;

    /* Restore SPI config (8-bit, no RXONLY). */
    spi1_apply_cr1(0u);
    spi1_enable();
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
    (void)spi1_txrx_u8_oem(0x03u); /* READ */
    (void)spi1_txrx_u8_oem((uint8_t)(addr >> 16));
    (void)spi1_txrx_u8_oem((uint8_t)(addr >> 8));
    (void)spi1_txrx_u8_oem((uint8_t)(addr));
    for (uint32_t i = 0; i < len; ++i)
        out[i] = spi1_txrx_u8_oem(0x00u);
    spi_flash_cs_high();
}

void spi_flash_read_dma_to_lcd(uint32_t addr, uint32_t lcd_addr, uint16_t count)
{
    spi_flash_dma_to_lcd(addr, lcd_addr, count);
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

static uint8_t g_spi_flash_sector_buf[SPI_FLASH_SECTOR_SIZE] __attribute__((aligned(4)));

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
     * OEM bootloader checks only byte[0] at 0x3FF080 == 0xAA. Avoid erasing
     * the sector; a single-byte program is enough when the byte is 0xFF/0xAA.
     */
    uint8_t cur = 0xFFu;
    spi_flash_read(SPI_FLASH_BOOTMODE_FLAG_ADDR, &cur, 1u);
    spi_flash_stage_mark(0xB201);
    if (cur == 0xAAu)
    {
        spi_flash_stage_mark(0xB205);
        return;
    }
    if ((cur & 0xAAu) != 0xAAu)
    {
        /*
         * Byte contains 0->1 transitions relative to 0xAA.
         * Fall back to read-modify-erase-write so recovery path remains writable.
         */
        const uint8_t flag = 0xAAu;
        spi_flash_update_bytes(SPI_FLASH_BOOTMODE_FLAG_ADDR, &flag, 1u);
        spi_flash_stage_mark(0xB206);
        return;
    }
    const uint8_t flag = 0xAAu;
    spi_flash_page_program(SPI_FLASH_BOOTMODE_FLAG_ADDR, &flag, 1u);
    spi_flash_stage_mark(0xB202);
}
