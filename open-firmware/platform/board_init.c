#include "platform/board_init.h"

#include "drivers/st7789_8080.h"
#include "platform/hw.h"
#include "platform/mmio.h"
#include "platform/time.h"
#include "ui_lcd.h"

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

#define LCD_CMD_ADDR  0x60000000u
#define LCD_DATA_ADDR 0x60020000u

#define ADC1_BASE 0x40012400u
#define ADC_CR1   (ADC1_BASE + 0x04u)
#define ADC_CR2   (ADC1_BASE + 0x08u)
#define ADC_SMPR2 (ADC1_BASE + 0x10u)
#define ADC_SQR1  (ADC1_BASE + 0x2Cu)
#define ADC_SQR3  (ADC1_BASE + 0x34u)

static inline void board_stage_mark(uint32_t value)
{
    (void)value;
}

static void platform_delay_ms(uint32_t ms)
{
    uint32_t start = g_ms;
    while ((uint32_t)(g_ms - start) < ms)
        platform_time_poll_1ms();
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

void platform_nvic_init(void)
{
    mmio_write32(SCB_AIRCR, SCB_AIRCR_VECTKEY | 0x500u);
}

static void platform_flash_control_pin_init(void)
{
    board_stage_mark(0xB110);
    mmio_write32(RCC_APB2ENR, mmio_read32(RCC_APB2ENR) | RCC_APB2ENR_IOPB);
    gpio_configure_mask(GPIOB_BASE, (uint16_t)(1u << 1), 0x10u, 0x02u);
    gpio_set_bits(GPIOB_BASE, (uint16_t)(1u << 1));
}

static void platform_ble_control_pins_init(void)
{
    board_stage_mark(0xB120);
    mmio_write32(RCC_APB2ENR, mmio_read32(RCC_APB2ENR) | RCC_APB2ENR_IOPA | RCC_APB2ENR_IOPC);

    gpio_configure_mask(GPIOA_BASE, (uint16_t)((1u << 11) | (1u << 12)), 0x10u, 0x02u);
    gpio_set_bits(GPIOA_BASE, (uint16_t)(1u << 12));
    gpio_clear_bits(GPIOA_BASE, (uint16_t)(1u << 11));

    gpio_configure_mask(GPIOC_BASE, (uint16_t)(1u << 12), 0x10u, 0x02u);
    gpio_set_bits(GPIOC_BASE, (uint16_t)(1u << 12));
}

static void platform_buttons_init(void)
{
    board_stage_mark(0xB130);
    mmio_write32(RCC_APB2ENR, mmio_read32(RCC_APB2ENR) | RCC_APB2ENR_IOPC);
    gpio_configure_mask(GPIOC_BASE, 0x007Fu, 0x48u, 0x02u); /* PC0-6 pull-up */
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

    mmio_write32(FSMC_BCR1, 0x00001014u);
    mmio_write32(FSMC_BTR1, 0x00000101u);
    mmio_write32(FSMC_BWTR1, 0x0FFFFFFFu);
    mmio_write32(FSMC_BCR1, mmio_read32(FSMC_BCR1) | 1u);
}

static inline void lcd_write_cmd(uint8_t v)
{
    *(volatile uint16_t *)LCD_CMD_ADDR = v;
}

static inline void lcd_write_data(uint8_t v)
{
    *(volatile uint16_t *)LCD_DATA_ADDR = v;
}

static void platform_lcd_init_oem_8080(void)
{
    board_stage_mark(0xB160);
    /* Reset line on PB1: high -> low -> high (OEM timing). */
    gpio_set_bits(GPIOB_BASE, (uint16_t)(1u << 1));
    platform_delay_ms(1u);
    gpio_clear_bits(GPIOB_BASE, (uint16_t)(1u << 1));
    platform_delay_ms(10u);
    gpio_set_bits(GPIOB_BASE, (uint16_t)(1u << 1));
    platform_delay_ms(50u);

    st7789_8080_bus_t bus = {
        .write_cmd = lcd_write_cmd,
        .write_data = lcd_write_data,
        .delay_ms = platform_delay_ms,
    };

    st7789_8080_init_oem(&bus);
    board_stage_mark(0xB16F);
}

static void platform_backlight_init(uint8_t level)
{
    board_stage_mark(0xB170);
    uint32_t apb2 = mmio_read32(RCC_APB2ENR);
    apb2 |= RCC_APB2ENR_IOPA | RCC_APB2ENR_TIM1;
    mmio_write32(RCC_APB2ENR, apb2);

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
}

static void platform_uart_pins_init(void)
{
    board_stage_mark(0xB180);
    uint32_t apb2 = mmio_read32(RCC_APB2ENR);
    apb2 |= RCC_APB2ENR_IOPA | RCC_APB2ENR_USART1;
    mmio_write32(RCC_APB2ENR, apb2);

    uint32_t apb1 = mmio_read32(RCC_APB1ENR);
    apb1 |= RCC_APB1ENR_USART2;
    mmio_write32(RCC_APB1ENR, apb1);

    gpio_configure_mask(GPIOA_BASE, 0x0200u, 0x18u, 0x02u); /* PA9  USART1_TX */
    gpio_configure_mask(GPIOA_BASE, 0x0400u, 0x48u, 0x02u); /* PA10 USART1_RX */
    gpio_configure_mask(GPIOA_BASE, 0x0004u, 0x18u, 0x02u); /* PA2  USART2_TX */
    gpio_configure_mask(GPIOA_BASE, 0x0008u, 0x48u, 0x02u); /* PA3  USART2_RX */

    mmio_write32(RCC_APB2RSTR, mmio_read32(RCC_APB2RSTR) | (1u << 14));
    mmio_write32(RCC_APB2RSTR, mmio_read32(RCC_APB2RSTR) & ~(1u << 14));
    mmio_write32(RCC_APB1RSTR, mmio_read32(RCC_APB1RSTR) | (1u << 17));
    mmio_write32(RCC_APB1RSTR, mmio_read32(RCC_APB1RSTR) & ~(1u << 17));
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

    mmio_write32(ADC_CR1, 0u);
    mmio_write32(ADC_CR2, 0u);
    mmio_write32(ADC_SMPR2, (mmio_read32(ADC_SMPR2) & ~0x7u) | 0x7u);
    mmio_write32(ADC_SQR1, 0u);
    mmio_write32(ADC_SQR3, 0u);

    /* Power on + calibrate. */
    mmio_write32(ADC_CR2, mmio_read32(ADC_CR2) | 0x1u);
    platform_delay_ms(1u);
    mmio_write32(ADC_CR2, mmio_read32(ADC_CR2) | 0x8u);
    while (mmio_read32(ADC_CR2) & 0x8u)
        ;
    mmio_write32(ADC_CR2, mmio_read32(ADC_CR2) | 0x4u);
    while (mmio_read32(ADC_CR2) & 0x4u)
        ;
}

void platform_board_init(void)
{
    board_stage_mark(0xB100);
    platform_flash_control_pin_init();
    platform_ble_control_pins_init();
    platform_buttons_init();

    platform_lcd_bus_pins_init();
    platform_fsmc_init();
    platform_lcd_init_oem_8080();
    ui_lcd_fill_rect(0u, 0u, 240u, 320u, 0u);

    platform_backlight_init(5u);
    platform_uart_pins_init();
    platform_adc_init();
    board_stage_mark(0xB1FF);

    /* OEM app provides the time base in platform_timebase_init_oem(). */
}
