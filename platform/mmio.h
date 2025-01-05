#ifndef OPEN_FIRMWARE_PLATFORM_MMIO_H
#define OPEN_FIRMWARE_PLATFORM_MMIO_H

#include <stdint.h>

/* ARM Cortex-M memory barriers for peripheral access */
static inline void mmio_dsb(void)
{
    __asm__ volatile("dsb 0xF" ::: "memory");
}

static inline void mmio_dmb(void)
{
    __asm__ volatile("dmb 0xF" ::: "memory");
}

static inline void mmio_isb(void)
{
    __asm__ volatile("isb 0xF" ::: "memory");
}

static inline void mmio_write32(uint32_t addr, uint32_t value)
{
    *(volatile uint32_t *)addr = value;
}

static inline uint32_t mmio_read32(uint32_t addr)
{
    return *(volatile uint32_t *)addr;
}

static inline void mmio_write8(uint32_t addr, uint8_t value)
{
    *(volatile uint8_t *)addr = value;
}

static inline uint8_t mmio_read8(uint32_t addr)
{
    return *(volatile uint8_t *)addr;
}

#endif

