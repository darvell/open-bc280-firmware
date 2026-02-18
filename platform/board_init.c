#include "platform/board_init.h"

#include "drivers/st7789_8080.h"
#include "platform/early_init.h"
#include "platform/hw.h"
#include "platform/mmio.h"
#include "platform/time.h"
#include "storage/boot_stage.h"
#include "boot_log.h"
#include "ui_lcd.h"
#include "ui_display.h"

#define RCC_APB2ENR_AFIO   (1u << 0)
#define RCC_APB2ENR_IOPA   (1u << 2)
#define RCC_APB2ENR_IOPB   (1u << 3)
#define RCC_APB2ENR_IOPC   (1u << 4)
#define RCC_APB2ENR_IOPD   (1u << 5)
#define RCC_APB2ENR_IOPE   (1u << 6)
#define RCC_APB2ENR_ADC1   (1u << 9)
#define RCC_APB2ENR_TIM1   (1u << 11)
#define RCC_APB2ENR_SPI1   (1u << 12)
#define RCC_APB2ENR_USART1 (1u << 14)

#define RCC_APB1ENR_USART2 (1u << 17)
#define RCC_APB1ENR_TIM2   (1u << 0)

#define RCC_AHBENR_FSMC    (1u << 8)

#define ADC1_BASE 0x40012400u
#define ADC_CR1   (ADC1_BASE + 0x04u)
#define ADC_CR2   (ADC1_BASE + 0x08u)
#define ADC_SMPR2 (ADC1_BASE + 0x10u)
#define ADC_SQR1  (ADC1_BASE + 0x2Cu)
#define ADC_SQR3  (ADC1_BASE + 0x34u)

static inline void board_stage_mark(uint32_t value)
{
    boot_stage_log(value);
    boot_log_stage(value);
}

static void platform_delay_ms(uint32_t ms)
{
    uint32_t start = g_ms;
    while ((uint32_t)(g_ms - start) < ms)
    {
        platform_time_poll_1ms();
        /* Keep IWDG alive if bootloader left it running. */
        mmio_write32(IWDG_KR, IWDG_KR_FEED);
    }
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

static void gpio_set_bits(uint32_t base, uint16_t mask)
{
    mmio_write32(GPIO_BSRR(base), mask);
}

static void gpio_clear_bits(uint32_t base, uint16_t mask)
{
    mmio_write32(GPIO_BRR(base), mask);
}

void platform_key_output_set(uint8_t on)
{
    /* Make this safe to call even during early boot or reboot paths. */
    mmio_write32(RCC_APB2ENR, mmio_read32(RCC_APB2ENR) | RCC_APB2ENR_IOPB);
    gpio_configure_mask(GPIOB_BASE, (uint16_t)(1u << 1), 0x10u, 0x02u); /* PB1 output PP */
    if (on)
        gpio_set_bits(GPIOB_BASE, (uint16_t)(1u << 1));
    else
        gpio_clear_bits(GPIOB_BASE, (uint16_t)(1u << 1));
}

void platform_nvic_init(void)
{
    mmio_write32(SCB_AIRCR, SCB_AIRCR_VECTKEY | 0x500u);
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

void platform_uart_irq_init(void)
{
#if !defined(HOST_TEST)
    /* Match OEM USART1/2 priority (grouping from AIRCR=0x500 -> priority=0x90). */
    const uint8_t prio = 0x90u;
    nvic_set_priority(37u, prio);
    nvic_set_priority(38u, prio);
    mmio_write32(NVIC_ISER1, (1u << (37u - 32u)) | (1u << (38u - 32u)));
#endif
}

static void platform_power_hold_pin_init(void)
{
    board_stage_mark(0xB110);
    /* OEM app v2.5.1: PB1 is configured early and driven low. Treat as "KEY"/enable,
     * not a hard power latch (OEM bootloader may still leave it high). */
    platform_key_output_set(0u);
}

void platform_ble_control_pins_init(void)
{
    board_stage_mark(0xB120);
    /*
     * BLE module control straps (OEM v2.3.0 ble_control_pins_init @ 0x80111E4):
     * - PA12: driven HIGH (BSRR)
     * - PA11: driven LOW (BRR)
     * - PC12: driven LOW (BRR)
     * No reset pulse — OEM just sets pin states directly.
     */
    mmio_write32(RCC_APB2ENR,
                mmio_read32(RCC_APB2ENR) | RCC_APB2ENR_IOPA | RCC_APB2ENR_IOPC);

    /* Configure strap pins as outputs (push-pull @ 2MHz). */
    gpio_configure_mask(GPIOA_BASE, (uint16_t)((1u << 11) | (1u << 12)), 0x10u, 0x02u);
    gpio_configure_mask(GPIOC_BASE, (uint16_t)(1u << 12), 0x10u, 0x02u);

    /* OEM v2.3.0 ble_control_pins_init (0x80111E4):
     * PA12 HIGH (BSRR), PA11 LOW (BRR), PC12 LOW (BRR). No reset pulse. */
    gpio_set_bits(GPIOA_BASE, (uint16_t)(1u << 12));
    gpio_clear_bits(GPIOA_BASE, (uint16_t)(1u << 11));
    gpio_clear_bits(GPIOC_BASE, (uint16_t)(1u << 12));
}

static void platform_ble_pins_ensure_output(void)
{
    /* Keep this safe to call even if clocks/pins are partially configured. */
    mmio_write32(RCC_APB2ENR,
                mmio_read32(RCC_APB2ENR) | RCC_APB2ENR_IOPA | RCC_APB2ENR_IOPC);
    gpio_configure_mask(GPIOA_BASE, (uint16_t)((1u << 11) | (1u << 12)), 0x10u, 0x02u);
    gpio_configure_mask(GPIOC_BASE, (uint16_t)(1u << 12), 0x10u, 0x02u);
}

void platform_ble_pa11_set(uint8_t high)
{
    platform_ble_pins_ensure_output();
    if (high)
        gpio_set_bits(GPIOA_BASE, (uint16_t)(1u << 11));
    else
        gpio_clear_bits(GPIOA_BASE, (uint16_t)(1u << 11));
}

void platform_ble_pa12_set(uint8_t high)
{
    platform_ble_pins_ensure_output();
    if (high)
        gpio_set_bits(GPIOA_BASE, (uint16_t)(1u << 12));
    else
        gpio_clear_bits(GPIOA_BASE, (uint16_t)(1u << 12));
}

void platform_ble_pc12_set(uint8_t high)
{
    platform_ble_pins_ensure_output();
    if (high)
        gpio_set_bits(GPIOC_BASE, (uint16_t)(1u << 12));
    else
        gpio_clear_bits(GPIOC_BASE, (uint16_t)(1u << 12));
}

uint8_t platform_ble_pa11_get(void)
{
    return (mmio_read32(GPIO_ODR(GPIOA_BASE)) & (1u << 11)) ? 1u : 0u;
}

uint8_t platform_ble_pa12_get(void)
{
    return (mmio_read32(GPIO_ODR(GPIOA_BASE)) & (1u << 12)) ? 1u : 0u;
}

uint8_t platform_ble_pc12_get(void)
{
    return (mmio_read32(GPIO_ODR(GPIOC_BASE)) & (1u << 12)) ? 1u : 0u;
}

void platform_ble_reset_pulse(uint32_t low_ms)
{
    /* Treat PA12 as active-low reset; mirrors OEM "disconnect" behavior. */
    platform_ble_pa12_set(0u);
    platform_delay_ms(low_ms ? low_ms : 10u);
    platform_ble_pa12_set(1u);
}

void platform_buttons_init(void)
{
    board_stage_mark(0xB130);
    mmio_write32(RCC_APB2ENR, mmio_read32(RCC_APB2ENR) | RCC_APB2ENR_IOPC);
    /* OEM app: PC0-4 inputs with pull-up. */
    gpio_configure_mask(GPIOC_BASE, 0x001Fu, 0x48u, 0x00u);
}

void platform_gpioc_aux_init(void)
{
    board_stage_mark(0xB135);
    mmio_write32(RCC_APB2ENR, mmio_read32(RCC_APB2ENR) | RCC_APB2ENR_IOPC);

    /* OEM app v2.5.1 (`sub_8018F40` @ 0x8018F40):
     * - PC5/PC6 configured as output open-drain (mode nibble 0x5 => 10MHz OD).
     * - PC5/PC6 ODR bits forced high via GPIOC_BSRR (released/high for open-drain).
     *
     * The real semantics of PC5/PC6 are unknown (likely accessory/harness).
     * What matters for alignment is matching the observable register writes.
     */
    gpio_configure_mask(GPIOC_BASE, 0x0060u, 0x14u, 0x01u);
    gpio_set_bits(GPIOC_BASE, 0x0060u);
}
static void platform_lcd_bus_pins_init(void)
{
    board_stage_mark(0xB140);
    uint32_t apb2 = mmio_read32(RCC_APB2ENR);
    apb2 |= RCC_APB2ENR_IOPA | RCC_APB2ENR_IOPB | RCC_APB2ENR_IOPD | RCC_APB2ENR_IOPE;
    mmio_write32(RCC_APB2ENR, apb2);

    gpio_configure_mask(GPIOA_BASE, 0x0100u, 0x10u, 0x02u);  /* PA8 */
    gpio_configure_mask(GPIOB_BASE, 0x0001u, 0x10u, 0x02u);  /* PB0 */
    gpio_configure_mask(GPIOD_BASE, 0xCFB3u, 0x18u, 0x02u);  /* PD0/1/4/5/7..11/14/15 */
    gpio_configure_mask(GPIOE_BASE, 0xFF80u, 0x18u, 0x02u);  /* PE7..15 */
}

static void platform_fsmc_init(void)
{
    board_stage_mark(0xB150);
    mmio_write32(RCC_AHBENR, mmio_read32(RCC_AHBENR) | RCC_AHBENR_FSMC);

    /*
     * OEM app (v2.5.1) FSMC timing:
     * - BCR1 = 0x00001014
     * - BTR1 = 0x00000101
     * - BWTR1 = 0x0FFFFFFF
     *
     * Evidence: OEM config table writer (`sub_8018FD4`) called from `sub_8019C50`.
     * Keep these values to match the OEM bus timing exactly.
     */
    mmio_write32(FSMC_BCR1, 0x00001014u);
    mmio_write32(FSMC_BTR1, 0x00000101u);
    mmio_write32(FSMC_BWTR1, 0x0FFFFFFFu);
    mmio_write32(FSMC_BCR1, mmio_read32(FSMC_BCR1) | 1u);

    /* Ensure FSMC configuration completes before any LCD access */
    mmio_dsb();
}

static inline void lcd_write_cmd(uint8_t v)
{
    *(volatile uint16_t *)LCD_CMD_ADDR = v;
}

static inline void lcd_write_data(uint8_t v)
{
    *(volatile uint16_t *)LCD_DATA_ADDR = v;
}

static inline void lcd_write_data16(uint16_t v)
{
    *(volatile uint16_t *)LCD_DATA_ADDR = v;
}

static void platform_lcd_init_oem_8080(void)
{
    board_stage_mark(0xB160);
    /* Reset line on PB0: high -> low -> high (matches OEM app timing). */
    gpio_set_bits(GPIOB_BASE, (uint16_t)(1u << 0));
    platform_delay_ms(1u);
    gpio_clear_bits(GPIOB_BASE, (uint16_t)(1u << 0));
    platform_delay_ms(10u);
    gpio_set_bits(GPIOB_BASE, (uint16_t)(1u << 0));
    platform_delay_ms(50u);

    st7789_8080_bus_t bus = {
        .write_cmd = lcd_write_cmd,
        .write_data = lcd_write_data,
        .write_data16 = lcd_write_data16,
        .delay_ms = platform_delay_ms,
    };

    st7789_8080_init_oem(&bus);
    board_stage_mark(0xB16F);
}

void platform_backlight_set_level(uint8_t level)
{
    if (level > 5u)
        level = 5u;
    mmio_write32(TIM_CCR1(TIM1_BASE), (uint32_t)level * 20u);
}

static void platform_backlight_init(uint8_t level)
{
    board_stage_mark(0xB170);
    uint32_t apb2 = mmio_read32(RCC_APB2ENR);
    apb2 |= RCC_APB2ENR_AFIO | RCC_APB2ENR_IOPA | RCC_APB2ENR_TIM1;
    mmio_write32(RCC_APB2ENR, apb2);

    /* Force PA8 high briefly, then switch to TIM1 CH1 AF output. */
    /* OEM app (v2.5.1): PA8 uses extend=0x02 (2MHz) for both output and AF-PP config. */
    gpio_configure_mask(GPIOA_BASE, 0x0100u, 0x10u, 0x02u); /* PA8 output PP */
    gpio_set_bits(GPIOA_BASE, 0x0100u);
    platform_delay_ms(1u);
    gpio_configure_mask(GPIOA_BASE, 0x0100u, 0x18u, 0x02u); /* PA8 AF PP */

    mmio_write32(TIM_CR1(TIM1_BASE), 0u);
    mmio_write32(TIM_PSC(TIM1_BASE), 71u);
    mmio_write32(TIM_ARR(TIM1_BASE), 99u);
    mmio_write32(TIM_CCR1(TIM1_BASE), (level > 5u) ? 100u : (uint32_t)level * 20u);
    mmio_write32(TIM_CCMR1(TIM1_BASE), (6u << 4) | (1u << 3)); /* PWM1 + preload */
    mmio_write32(TIM_CCER(TIM1_BASE), 1u); /* CC1E */
    mmio_write32(TIM_BDTR(TIM1_BASE), 1u << 15); /* MOE */
    mmio_write32(TIM_EGR(TIM1_BASE), 1u); /* UG */
    mmio_write32(TIM_CR1(TIM1_BASE), (1u << 7) | 1u); /* ARPE + CEN */
    platform_backlight_set_level(level);
}

void platform_ble_uart_pins_init(void)
{
    board_stage_mark(0xB180);
    uint32_t apb2 = mmio_read32(RCC_APB2ENR);
    apb2 |= RCC_APB2ENR_IOPA | RCC_APB2ENR_USART1;
    mmio_write32(RCC_APB2ENR, apb2);

    gpio_configure_mask(GPIOA_BASE, 0x0200u, 0x18u, 0x02u); /* PA9  USART1_TX */
    gpio_configure_mask(GPIOA_BASE, 0x0400u, 0x48u, 0x00u); /* PA10 USART1_RX (input pull-up) */

    /* If the boot monitor already configured USART1, do not reset it here.
     * Resetting USART1 drops the BLE UART session right after 'continue boot'. */
    if (!platform_uart1_was_inited_early())
    {
        mmio_write32(RCC_APB2RSTR, mmio_read32(RCC_APB2RSTR) | (1u << 14));
        mmio_write32(RCC_APB2RSTR, mmio_read32(RCC_APB2RSTR) & ~(1u << 14));
    }
}

void platform_motor_uart_pins_init(void)
{
    board_stage_mark(0xB182);
    uint32_t apb2 = mmio_read32(RCC_APB2ENR);
    apb2 |= RCC_APB2ENR_IOPA;
    mmio_write32(RCC_APB2ENR, apb2);

    uint32_t apb1 = mmio_read32(RCC_APB1ENR);
    apb1 |= RCC_APB1ENR_USART2;
    mmio_write32(RCC_APB1ENR, apb1);

    gpio_configure_mask(GPIOA_BASE, 0x0004u, 0x18u, 0x02u); /* PA2  USART2_TX */
    gpio_configure_mask(GPIOA_BASE, 0x0008u, 0x48u, 0x00u); /* PA3  USART2_RX (input pull-up) */

    mmio_write32(RCC_APB1RSTR, mmio_read32(RCC_APB1RSTR) | (1u << 17));
    mmio_write32(RCC_APB1RSTR, mmio_read32(RCC_APB1RSTR) & ~(1u << 17));
}

void platform_uart_pins_init(void)
{
    /* Legacy entrypoint: configure both UART pin groups. Prefer calling the
     * specific init function at the point where each UART is first used. */
    platform_ble_uart_pins_init();
    platform_motor_uart_pins_init();
}

static void platform_adc_init(void)
{
    uint32_t apb2 = mmio_read32(RCC_APB2ENR);
    apb2 |= RCC_APB2ENR_IOPA | RCC_APB2ENR_ADC1;
    mmio_write32(RCC_APB2ENR, apb2);

    /* ADC prescaler /6 (OEM uses 0x8000 on CFGR). */
    mmio_write32(RCC_CFGR, (mmio_read32(RCC_CFGR) & ~0x0000C000u) | 0x00008000u);

    /* PA0 analog input (battery divider). */
    gpio_configure_mask(GPIOA_BASE, 0x0001u, 0x00u, 0x00u);

    /* OEM-style ADC1 init sequence (mirrors app 2.2.5). */
    mmio_write32(ADC_CR1, mmio_read32(ADC_CR1) & 0xFFF0FEFFu);
    mmio_write32(ADC_CR2, (mmio_read32(ADC_CR2) & 0xFFF1F7FDu) | 0x000E0000u);
    mmio_write32(ADC_SQR1, mmio_read32(ADC_SQR1) & 0xFF0FFFFFu); /* L=0 (1 conversion) */
    mmio_write32(ADC_SMPR2, (mmio_read32(ADC_SMPR2) & ~0x7u) | 0x5u);
    mmio_write32(ADC_SQR3, mmio_read32(ADC_SQR3) & ~0x1Fu); /* channel 0 */

    /* Power on + reset calibration + calibration (OEM ordering). */
    mmio_write32(ADC_CR2, mmio_read32(ADC_CR2) | 0x1u);
    mmio_write32(ADC_CR2, mmio_read32(ADC_CR2) | 0x8u);
    while (mmio_read32(ADC_CR2) & 0x8u)
    {
        mmio_write32(IWDG_KR, IWDG_KR_FEED);
    }
    mmio_write32(ADC_CR2, mmio_read32(ADC_CR2) | 0x4u);
    while (mmio_read32(ADC_CR2) & 0x4u)
    {
        mmio_write32(IWDG_KR, IWDG_KR_FEED);
    }

    /* OEM enables bits 0x500000 after calibration. */
    mmio_write32(ADC_CR2, mmio_read32(ADC_CR2) | 0x500000u);
}

void platform_board_init(void)
{
    board_stage_mark(0xB100);
    platform_power_hold_pin_init();

    platform_lcd_bus_pins_init();
    platform_fsmc_init();
    platform_lcd_init_oem_8080();
    ui_lcd_fill_rect(0u, 0u, DISP_W, DISP_H, 0u);

    /* Turn backlight on immediately so any crash after this point is visible. */
    platform_backlight_init(5u);
    platform_adc_init();

    /* OEM v2.5.1 brings up BLE module control pins + UART1 after LCD/backlight init. */
    platform_ble_control_pins_init();
    platform_ble_uart_pins_init();

    /* Buttons are used early for safe-mode in open-firmware, but keep the OEM
     * wiring/mode configuration consistent here as well. */
    platform_buttons_init();
    board_stage_mark(0xB1FF);

    /* OEM app provides the time base in platform_timebase_init_oem(). */
}
