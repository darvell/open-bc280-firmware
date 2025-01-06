#include "platform/time.h"

#include "platform/clock.h"
#include "platform/hw.h"
#include "platform/mmio.h"

volatile uint32_t g_ms;

void SysTick_Handler(void)
{
    /* OEM firmware leaves SysTick empty; timebase is TIM2. */
}

void TIM2_IRQHandler(void)
{
    uint32_t sr = mmio_read32(TIM_SR(TIM2_BASE));
    uint32_t dier = mmio_read32(TIM_DIER(TIM2_BASE));
    if ((sr & 1u) && (dier & 1u))
    {
        /* Clear UIF by writing inverted mask (OEM pattern). */
        mmio_write32(TIM_SR(TIM2_BASE), ~1u);
        g_ms += 5u;
    }
}

void platform_time_poll_1ms(void)
{
    /*
     * OEM uses TIM2 update interrupts (5ms). Polling UIF keeps g_ms moving even
     * if IRQ delivery is unavailable.
     *
     * NOTE: UIF does not accumulate multiple wraps; callers must poll faster
     * than the tick rate to avoid losing time.
     */
    uint32_t sr = mmio_read32(TIM_SR(TIM2_BASE));
    if ((sr & 1u) && (mmio_read32(TIM_DIER(TIM2_BASE)) & 1u))
    {
        mmio_write32(TIM_SR(TIM2_BASE), ~1u);
        g_ms += 5u;
    }
}

void platform_timebase_init_oem(void)
{
    /* Disable SysTick; OEM app uses TIM2 as the time base. */
    mmio_write32(SYST_CSR, 0u);

    /* Enable TIM2 clock (APB1ENR bit0) and reset. */
    uint32_t apb1 = mmio_read32(RCC_APB1ENR);
    mmio_write32(RCC_APB1ENR, apb1 | (1u << 0));
    uint32_t rstr = mmio_read32(RCC_APB1RSTR);
    mmio_write32(RCC_APB1RSTR, rstr | (1u << 0));
    mmio_write32(RCC_APB1RSTR, rstr & ~(1u << 0));

    /* OEM init: PSC=9, ARR=35999 => 200Hz tick (~5ms) at 72MHz timer clock. */
    mmio_write32(TIM_PSC(TIM2_BASE), 9u);
    mmio_write32(TIM_ARR(TIM2_BASE), 35999u);
    mmio_write32(TIM_CNT(TIM2_BASE), 0u);
    mmio_write32(TIM_DIER(TIM2_BASE), mmio_read32(TIM_DIER(TIM2_BASE)) | 1u);
    mmio_write32(TIM_EGR(TIM2_BASE), 1u);
    mmio_write32(TIM_CR1(TIM2_BASE), mmio_read32(TIM_CR1(TIM2_BASE)) | 1u);

    /* NVIC enable for IRQ28 (TIM2). */
    mmio_write32(NVIC_ISER0, mmio_read32(NVIC_ISER0) | (1u << 28));
}
