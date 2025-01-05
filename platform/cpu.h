#ifndef OPEN_FIRMWARE_PLATFORM_CPU_H
#define OPEN_FIRMWARE_PLATFORM_CPU_H

#include <stdint.h>

static inline void disable_irqs(void)
{
    __asm__ volatile("cpsid i" ::: "memory");
}

static inline void enable_irqs(void)
{
    __asm__ volatile("cpsie i" ::: "memory");
}

static inline void wfi(void)
{
    __asm__ volatile("wfi" ::: "memory");
}

__attribute__((unused)) static inline void set_msp(uint32_t sp)
{
    __asm__ volatile("msr msp, %0" : : "r"(sp) : "memory");
}

#endif

