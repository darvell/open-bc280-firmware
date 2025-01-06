#include "platform/clock.h"

#include "platform/hw.h"
#include "platform/mmio.h"

static void clock_delay_cycles(volatile uint32_t cycles)
{
    while (cycles--)
        __asm__ volatile("nop");
}

static int rcc_wait_flag(uint32_t mask, uint32_t limit)
{
    while (limit--)
    {
        if (mmio_read32(RCC_CR) & mask)
            return 1;
    }
    return 0;
}

static int rcc_wait_hse_ready(void)
{
    /* RCC_CR HSERDY bit = 17. OEM uses ~1280 polls + extra delay. */
    for (uint32_t i = 0; i < 1280u; ++i)
    {
        if (mmio_read32(RCC_CR) & (1u << 17))
            break;
    }
    clock_delay_cycles(5000u);
    return (mmio_read32(RCC_CR) & (1u << 17)) != 0u;
}

void platform_clock_init(void)
{
    /* Reset RCC to default state (OEM pattern). */
    mmio_write32(RCC_CR, mmio_read32(RCC_CR) | 0x00000001u);      /* HSION */
    mmio_write32(RCC_CFGR, mmio_read32(RCC_CFGR) & 0xE8FF0000u);
    mmio_write32(RCC_CR, mmio_read32(RCC_CR) & 0xFEF6FFFFu);      /* HSEON/CSS/PLL off */
    mmio_write32(RCC_CR, mmio_read32(RCC_CR) & ~0x00040000u);     /* HSEBYP off */
    mmio_write32(RCC_CFGR, mmio_read32(RCC_CFGR) & 0x1700FFFFu);
    mmio_write32(RCC_CIR, 0x009F0000u);

    /* Enable HSE and wait (fallback to HSI/2 if it fails). */
    mmio_write32(RCC_CR, mmio_read32(RCC_CR) & ~0x00050000u);     /* HSEON/HSEBYP off */
    mmio_write32(RCC_CR, mmio_read32(RCC_CR) | 0x00010000u);      /* HSEON */
    clock_delay_cycles(50000u);
    const int hse_ready = rcc_wait_hse_ready();

    /* Flash latency + prefetch (OEM uses 2 WS + prefetch). */
    mmio_write32(FLASH_ACR, (mmio_read32(FLASH_ACR) & 0x38u) | 0x2u);
    mmio_write32(FLASH_ACR, (mmio_read32(FLASH_ACR) & ~0x10u) | 0x10u);

    /* AHB=1, APB2=1, APB1=/2 (OEM values). */
    uint32_t cfgr = mmio_read32(RCC_CFGR);
    cfgr = (cfgr & ~0x000000F0u) | 0x00000000u; /* HPRE */
    cfgr = (cfgr & ~0x00003800u) | 0x00000000u; /* PPRE2 */
    cfgr = (cfgr & ~0x00000700u) | 0x00000400u; /* PPRE1 /2 */

    /* PLL configuration (OEM uses HSE * 9, fallback uses HSI/2 path). */
    cfgr &= 0x1FC0FFFFu;
    if (hse_ready)
    {
        cfgr |= 0x00010000u; /* PLLSRC = HSE */
        cfgr |= 0x001C0000u; /* PLLMUL = 9 */
    }
    else
    {
        cfgr |= 0x20040000u; /* OEM fallback constant */
    }
    mmio_write32(RCC_CFGR, cfgr);

    /* Enable PLL and wait for lock (PLLRDY bit 25). */
    mmio_write32(RCC_CR, mmio_read32(RCC_CR) | 0x01000000u);
    (void)rcc_wait_flag(1u << 25, 1000000u);

    /* OEM toggles RCC_MISC bits 4..5 around the clock switch. */
    mmio_write32(RCC_MISC, mmio_read32(RCC_MISC) | 0x30u);

    /* Switch SYSCLK to PLL and wait until SWS reports PLL (0x8). */
    mmio_write32(RCC_CFGR, (mmio_read32(RCC_CFGR) & ~0x3u) | 0x2u);
    while ((mmio_read32(RCC_CFGR) & 0xCu) != 0x8u)
        ;

    mmio_write32(RCC_MISC, mmio_read32(RCC_MISC) & ~0x30u);
}

uint32_t rcc_get_hclk_hz_fallback(void)
{
    /*
     * Clocking on BC280 comes from the OEM bootloader or open-firmware init.
     * Infer current HCLK from RCC->CFGR using STM32F1-ish bitfields observed in
     * the OEM combined image. If inference fails, fall back to 72MHz.
     */
    static const uint8_t k_hpre_shift[16] = {
        0, 0, 0, 0, 0, 0, 0, 0,
        1, 2, 3, 4, 6, 7, 8, 9
    };

    const uint32_t cfgr = mmio_read32(RCC_CFGR);
    const uint32_t sws = cfgr & 0x0Cu; /* SWS[3:2] */

    /* Observed on BC280 OEM image: 8MHz HSE, and HSI/2 path uses 4MHz. */
    const uint32_t HSE_HZ = 8000000u;
    const uint32_t HSI_HZ = 8000000u;

    uint32_t sysclk = HSI_HZ;
    if (sws == 0x04u) /* HSE */
    {
        sysclk = HSE_HZ;
    }
    else if (sws == 0x08u) /* PLL */
    {
        const uint32_t pll_bits = cfgr & 0x083C0000u; /* matches OEM mask */
        uint32_t mul = pll_bits >> 18;
        if (pll_bits & 0x08000000u)
            mul = (mul > 495u) ? (mul - 495u) : 0u;
        else
            mul += 2u;

        uint32_t base = HSI_HZ / 2u; /* HSI/2 */
        if (cfgr & 0x10000u) /* PLLSRC = HSE */
            base = (cfgr & 0x20000u) ? (HSE_HZ / 2u) : HSE_HZ;
        sysclk = base * mul;
    }

    const uint8_t hpre = (uint8_t)((cfgr >> 4) & 0x0Fu);
    uint32_t hclk = sysclk >> k_hpre_shift[hpre];

    /* Sanity clamp: expect something between 1MHz and 300MHz. */
    if (hclk < 1000000u || hclk > 300000000u)
        return 72000000u;
    return hclk;
}

uint32_t rcc_get_pclk_hz_fallback(uint8_t apb2)
{
    const uint32_t cfgr = mmio_read32(RCC_CFGR);
    uint32_t hclk = rcc_get_hclk_hz_fallback();

    /* APB prescaler decode (STM32F1-like): 0b0xx => /1, 0b1xx => /2/4/8/16. */
    uint32_t ppre = apb2 ? ((cfgr >> 11) & 0x7u) : ((cfgr >> 8) & 0x7u);
    uint32_t shift = (ppre & 0x4u) ? ((ppre & 0x3u) + 1u) : 0u;
    uint32_t pclk = hclk >> shift;
    if (pclk < 1000000u || pclk > 300000000u)
        return 72000000u;
    return pclk;
}
