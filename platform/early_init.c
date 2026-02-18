#include "platform/early_init.h"

#include "platform/hw.h"
#include "platform/mmio.h"

static uint8_t g_uart1_early_inited;

static void early_delay_cycles(volatile uint32_t cycles)
{
    while (cycles--)
        __asm__ volatile("nop");
}

static void gpio_configure_mask(uint32_t base, uint16_t mask, uint8_t mode_byte, uint8_t extend)
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

    /* Mirror OEM pull-up/down defaults (0x28 = pull-down, 0x48 = pull-up). */
    if (mode_byte == 0x28u)
        mmio_write32(GPIO_BRR(base), mask);
    else if (mode_byte == 0x48u)
        mmio_write32(GPIO_BSRR(base), mask);
}

void platform_ble_control_pins_init_early(void)
{
    /*
     * Minimal BLE control init used by the boot monitor.
     *
     * Match OEM strap behavior:
     * - PA11 low (strap)
     * - PC12 low (strap)
     * - PA12 reset line: pulse low->high for deterministic bring-up on warm resets
     *
     * This function is intentionally self-contained and does not depend on TIM2/g_ms.
     */
    mmio_write32(RCC_APB2ENR, mmio_read32(RCC_APB2ENR) | (1u << 2) | (1u << 4)); /* IOPA + IOPC */

    /* Configure strap pins as outputs. */
    gpio_configure_mask(GPIOA_BASE, (uint16_t)((1u << 11) | (1u << 12)), 0x10u, 0x02u);
    gpio_configure_mask(GPIOC_BASE, (uint16_t)(1u << 12), 0x10u, 0x02u);

    /* Strap defaults: PA11 low, PC12 low. */
    mmio_write32(GPIO_BRR(GPIOA_BASE), (uint16_t)(1u << 11));
    mmio_write32(GPIO_BRR(GPIOC_BASE), (uint16_t)(1u << 12));

    /* PA12 reset pulse: low, short delay, then high. */
    mmio_write32(GPIO_BRR(GPIOA_BASE), (uint16_t)(1u << 12));
    early_delay_cycles(500000u);
    mmio_write32(GPIO_BSRR(GPIOA_BASE), (uint16_t)(1u << 12));
}

void platform_uart1_pins_init_early(void)
{
    if (g_uart1_early_inited)
        return;

    /* Enable GPIOA + USART1 clocks. */
    mmio_write32(RCC_APB2ENR, mmio_read32(RCC_APB2ENR) | (1u << 2) | (1u << 14)); /* IOPA + USART1 */

    /* PA9 TX AF PP, PA10 RX input pull-up (OEM-like). */
    gpio_configure_mask(GPIOA_BASE, (uint16_t)(1u << 9), 0x18u, 0x02u);
    gpio_configure_mask(GPIOA_BASE, (uint16_t)(1u << 10), 0x48u, 0x00u);

    /* Reset USART1 once so we don't inherit a weird bootloader config. */
    mmio_write32(RCC_APB2RSTR, mmio_read32(RCC_APB2RSTR) | (1u << 14));
    mmio_write32(RCC_APB2RSTR, mmio_read32(RCC_APB2RSTR) & ~(1u << 14));

    g_uart1_early_inited = 1u;
}

uint8_t platform_uart1_was_inited_early(void)
{
    return g_uart1_early_inited;
}
