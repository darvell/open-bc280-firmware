#include "sim_mcu.h"

#include <stdlib.h>
#include <string.h>

#define RCC_BASE 0x40021000u
#define RCC_CSR  (RCC_BASE + 0x24u)
#define IWDG_BASE 0x40003000u
#define UART1_BASE 0x40013800u
#define UART2_BASE 0x40004400u
#define UART4_BASE 0x40004C00u
#define UART_SR(base) ((base) + 0x00u)
#define UART_DR(base) ((base) + 0x04u)
#define UART_BRR(base) ((base) + 0x08u)
#define UART_CR1(base) ((base) + 0x0Cu)

#define GPIOA_BASE 0x40010800u
#define GPIOB_BASE 0x40010C00u
#define GPIOC_BASE 0x40011000u
#define GPIOD_BASE 0x40011400u
#define GPIOE_BASE 0x40011800u

#define ADC1_BASE 0x40012400u

enum {
    UART_SR_TXE = 1u << 7,
    UART_SR_RXNE = 1u << 5,
};

typedef struct {
    uint32_t sr;
    uint32_t dr;
    uint32_t brr;
    uint32_t cr1;
    uint8_t rx_buf[2048];
    size_t rx_head;
    size_t rx_tail;
    uint8_t tx_buf[2048];
    size_t tx_head;
    size_t tx_tail;
} sim_uart_t;

struct sim_mcu {
    uint32_t rcc_csr;
    uint32_t iwdg_pr;
    uint32_t iwdg_rlr;
    uint32_t iwdg_kick_ms;
    uint8_t iwdg_started;
    uint8_t iwdg_unlocked;
    uint32_t adc_sr;
    uint16_t adc_values[18];
    uint16_t adc_last;
    uint32_t gpio_idr[5];
    uint32_t gpio_odr[5];
    sim_uart_t uart[3];
    uint8_t spi_flash[0x400000];
    uint32_t now_ms;
};

static sim_uart_t *uart_by_addr(sim_mcu_t *s, uint32_t addr)
{
    if (addr >= UART1_BASE && addr < UART1_BASE + 0x100)
        return &s->uart[0];
    if (addr >= UART2_BASE && addr < UART2_BASE + 0x100)
        return &s->uart[1];
    if (addr >= UART4_BASE && addr < UART4_BASE + 0x100)
        return &s->uart[2];
    return NULL;
}

static void uart_push_rx(sim_uart_t *u, const uint8_t *data, size_t len)
{
    for (size_t i = 0; i < len; ++i)
    {
        size_t next = (u->rx_head + 1u) % sizeof(u->rx_buf);
        if (next == u->rx_tail)
            break;
        u->rx_buf[u->rx_head] = data[i];
        u->rx_head = next;
    }
    if (u->rx_head != u->rx_tail)
        u->sr |= UART_SR_RXNE;
}

static size_t uart_pop_tx(sim_uart_t *u, uint8_t *out, size_t cap)
{
    size_t n = 0;
    while (u->tx_tail != u->tx_head && n < cap)
    {
        out[n++] = u->tx_buf[u->tx_tail];
        u->tx_tail = (u->tx_tail + 1u) % sizeof(u->tx_buf);
    }
    return n;
}

static void uart_push_tx(sim_uart_t *u, uint8_t value)
{
    size_t next = (u->tx_head + 1u) % sizeof(u->tx_buf);
    if (next != u->tx_tail)
    {
        u->tx_buf[u->tx_head] = value;
        u->tx_head = next;
    }
    u->sr |= UART_SR_TXE;
}

sim_mcu_t *sim_mcu_create(void)
{
    sim_mcu_t *s = (sim_mcu_t *)calloc(1, sizeof(*s));
    if (!s)
        return NULL;
    sim_mcu_reset(s);
    return s;
}

void sim_mcu_destroy(sim_mcu_t *s)
{
    if (s)
        free(s);
}

void sim_mcu_reset(sim_mcu_t *s)
{
    if (!s)
        return;
    memset(s, 0, sizeof(*s));
    s->rcc_csr = (1u << 27);
    for (int i = 0; i < 3; ++i)
        s->uart[i].sr = UART_SR_TXE;
    for (int ch = 0; ch < 18; ++ch)
        s->adc_values[ch] = 2000;
    s->adc_sr = 0;
    s->spi_flash[0x3FF080] = 0xAA;
}

void sim_mcu_step(sim_mcu_t *s, uint32_t dt_ms)
{
    if (!s)
        return;
    s->now_ms += dt_ms;
    if (s->iwdg_started && s->iwdg_rlr)
    {
        uint32_t timeout_ms = (uint32_t)((s->iwdg_rlr + 1u) * 4u / 40u);
        if (timeout_ms == 0)
            timeout_ms = 1;
        if (s->now_ms - s->iwdg_kick_ms > timeout_ms)
        {
            s->rcc_csr |= (1u << 29);
            sim_mcu_reset(s);
        }
    }
}

static uint32_t gpio_index(uint32_t base)
{
    switch (base)
    {
        case GPIOA_BASE: return 0;
        case GPIOB_BASE: return 1;
        case GPIOC_BASE: return 2;
        case GPIOD_BASE: return 3;
        case GPIOE_BASE: return 4;
        default: return 0xFFFFFFFFu;
    }
}

uint32_t sim_mcu_read32(sim_mcu_t *s, uint32_t addr)
{
    if (!s)
        return 0;
    if (addr == RCC_CSR)
        return s->rcc_csr;
    if (addr >= GPIOA_BASE && addr <= GPIOE_BASE + 0x0Cu)
    {
        uint32_t idx = gpio_index(addr & ~0x3FFu);
        if (idx < 5 && (addr & 0x3FFu) == 0x08u)
            return s->gpio_idr[idx];
        if (idx < 5 && (addr & 0x3FFu) == 0x0Cu)
            return s->gpio_odr[idx];
    }
    if ((addr >= UART1_BASE && addr < UART1_BASE + 0x100) ||
        (addr >= UART2_BASE && addr < UART2_BASE + 0x100) ||
        (addr >= UART4_BASE && addr < UART4_BASE + 0x100))
    {
        sim_uart_t *u = uart_by_addr(s, addr);
        if (!u)
            return 0;
        switch (addr & 0xFFu)
        {
            case 0x00: return u->sr;
            case 0x04:
                if (u->rx_tail != u->rx_head)
                {
                    uint8_t v = u->rx_buf[u->rx_tail];
                    u->rx_tail = (u->rx_tail + 1u) % sizeof(u->rx_buf);
                    if (u->rx_tail == u->rx_head)
                        u->sr &= ~UART_SR_RXNE;
                    return v;
                }
                return 0;
            case 0x08: return u->brr;
            case 0x0C: return u->cr1;
            default: return 0;
        }
    }
    if (addr == ADC1_BASE + 0x00u)
        return s->adc_sr;
    if (addr == ADC1_BASE + 0x4Cu)
    {
        /* Reading DR clears EOC in SR (STM32F1-like behavior). */
        s->adc_sr &= ~(1u << 1);
        return s->adc_last;
    }
    return 0;
}

void sim_mcu_write32(sim_mcu_t *s, uint32_t addr, uint32_t value)
{
    if (!s)
        return;
    if (addr == RCC_CSR)
    {
        if (value & (1u << 24))
            s->rcc_csr &= ~0xFE000000u;
        return;
    }
    if (addr >= GPIOA_BASE && addr <= GPIOE_BASE + 0x18u)
    {
        uint32_t idx = gpio_index(addr & ~0x3FFu);
        uint32_t off = addr & 0x3FFu;
        if (idx < 5 && off == 0x10u)
        {
            uint32_t set = value & 0xFFFFu;
            uint32_t rst = (value >> 16) & 0xFFFFu;
            s->gpio_odr[idx] |= set;
            s->gpio_odr[idx] &= ~rst;
        }
        else if (idx < 5 && off == 0x14u)
        {
            s->gpio_odr[idx] &= ~(value & 0xFFFFu);
        }
        return;
    }
    if ((addr >= UART1_BASE && addr < UART1_BASE + 0x100) ||
        (addr >= UART2_BASE && addr < UART2_BASE + 0x100) ||
        (addr >= UART4_BASE && addr < UART4_BASE + 0x100))
    {
        sim_uart_t *u = uart_by_addr(s, addr);
        if (!u)
            return;
        switch (addr & 0xFFu)
        {
            case 0x04:
                uart_push_tx(u, (uint8_t)value);
                break;
            case 0x08:
                u->brr = value;
                break;
            case 0x0C:
                u->cr1 = value;
                break;
            default:
                break;
        }
        return;
    }
    if (addr == IWDG_BASE + 0x00u)
    {
        if (value == 0x5555u)
            s->iwdg_unlocked = 1;
        else if (value == 0xCCCCu)
        {
            s->iwdg_started = 1;
            s->iwdg_kick_ms = s->now_ms;
        }
        else if (value == 0xAAAAu)
        {
            s->iwdg_kick_ms = s->now_ms;
        }
        return;
    }
    if (addr == IWDG_BASE + 0x04u && s->iwdg_unlocked)
    {
        s->iwdg_pr = value & 0x7u;
        return;
    }
    if (addr == IWDG_BASE + 0x08u && s->iwdg_unlocked)
    {
        s->iwdg_rlr = value & 0x0FFFu;
        return;
    }
    if (addr == ADC1_BASE + 0x08u)
    {
        if (value & (1u << 22))
        {
            s->adc_last = s->adc_values[0];
            s->adc_sr |= (1u << 1); /* EOC */
        }
        return;
    }
}

void sim_mcu_gpio_set_input(sim_mcu_t *s, char port, uint8_t pin, uint8_t level)
{
    if (!s || pin > 15)
        return;
    uint32_t idx = 0;
    switch (port)
    {
        case 'A': idx = 0; break;
        case 'B': idx = 1; break;
        case 'C': idx = 2; break;
        case 'D': idx = 3; break;
        case 'E': idx = 4; break;
        default: return;
    }
    if (level)
        s->gpio_idr[idx] |= (1u << pin);
    else
        s->gpio_idr[idx] &= ~(1u << pin);
}

uint16_t sim_mcu_gpio_get_idr(sim_mcu_t *s, char port)
{
    if (!s)
        return 0;
    uint32_t idx = 0;
    switch (port)
    {
        case 'A': idx = 0; break;
        case 'B': idx = 1; break;
        case 'C': idx = 2; break;
        case 'D': idx = 3; break;
        case 'E': idx = 4; break;
        default: return 0;
    }
    return (uint16_t)(s->gpio_idr[idx] & 0xFFFFu);
}

void sim_mcu_adc_set_channel(sim_mcu_t *s, uint8_t ch, uint16_t value)
{
    if (!s || ch >= 18)
        return;
    s->adc_values[ch] = value & 0x0FFFu;
}

size_t sim_mcu_uart_pop_tx(sim_mcu_t *s, int uart, uint8_t *out, size_t cap)
{
    if (!s || uart < 0 || uart > 2)
        return 0;
    return uart_pop_tx(&s->uart[uart], out, cap);
}

size_t sim_mcu_uart_push_rx(sim_mcu_t *s, int uart, const uint8_t *data, size_t len)
{
    if (!s || uart < 0 || uart > 2)
        return 0;
    uart_push_rx(&s->uart[uart], data, len);
    return len;
}

void sim_mcu_spi_flash_write(sim_mcu_t *s, uint32_t addr, const uint8_t *data, size_t len)
{
    if (!s || !data)
        return;
    if (addr + len > sizeof(s->spi_flash))
        return;
    memcpy(&s->spi_flash[addr], data, len);
}

void sim_mcu_spi_flash_read(sim_mcu_t *s, uint32_t addr, uint8_t *out, size_t len)
{
    if (!s || !out)
        return;
    if (addr + len > sizeof(s->spi_flash))
        return;
    memcpy(out, &s->spi_flash[addr], len);
}
