#include <stdint.h>

extern uint32_t _stack_top;
extern uint32_t _sbss;
extern uint32_t _ebss;
extern uint32_t _sdata;
extern uint32_t _edata;
extern uint32_t _sidata;

int main(void);

static void Default_Handler(void);

void Reset_Handler(void);
void SysTick_Handler(void);
void TIM2_IRQHandler(void);
void HardFault_Handler(void);

static inline void disable_irqs(void)
{
    __asm__ volatile("cpsid i" ::: "memory");
}

static inline void enable_fpu(void)
{
    /* Enable CP10/CP11 full access (Cortex-M4F FPU). */
    volatile uint32_t *cpacr = (volatile uint32_t *)0xE000ED88u;
    *cpacr |= (0xFu << 20);
    __asm__ volatile("dsb 0xF" ::: "memory");
    __asm__ volatile("isb 0xF" ::: "memory");
}

static void disable_all_external_irqs(void)
{
    volatile uint32_t *icer = (volatile uint32_t *)0xE000E180u; // NVIC_ICER0
    volatile uint32_t *icpr = (volatile uint32_t *)0xE000E280u; // NVIC_ICPR0
    for (int i = 0; i < 8; i++)
    {
        icer[i] = 0xFFFFFFFFu;
        icpr[i] = 0xFFFFFFFFu;
    }
}

__attribute__((section(".isr_vector")))
void (*const g_isr_vector[])(void) = {
    (void (*)(void))(&_stack_top),
    Reset_Handler,
    Default_Handler, /* NMI */
    HardFault_Handler, /* HardFault */
    Default_Handler, /* MemManage */
    Default_Handler, /* BusFault */
    Default_Handler, /* UsageFault */
    0,
    0,
    0,
    0,
    Default_Handler, /* SVCall */
    Default_Handler, /* DebugMonitor */
    0,
    Default_Handler, /* PendSV */
    SysTick_Handler,
    /* External IRQs (cover a reasonable span; most unused). */
    Default_Handler, /* IRQ0  */
    Default_Handler, /* IRQ1  */
    Default_Handler, /* IRQ2  */
    Default_Handler, /* IRQ3  */
    Default_Handler, /* IRQ4  */
    Default_Handler, /* IRQ5  */
    Default_Handler, /* IRQ6  */
    Default_Handler, /* IRQ7  */
    Default_Handler, /* IRQ8  */
    Default_Handler, /* IRQ9  */
    Default_Handler, /* IRQ10 */
    Default_Handler, /* IRQ11 */
    Default_Handler, /* IRQ12 */
    Default_Handler, /* IRQ13 */
    Default_Handler, /* IRQ14 */
    Default_Handler, /* IRQ15 */
    Default_Handler, /* IRQ16 */
    Default_Handler, /* IRQ17 */
    Default_Handler, /* IRQ18 */
    Default_Handler, /* IRQ19 */
    Default_Handler, /* IRQ20 */
    Default_Handler, /* IRQ21 */
    Default_Handler, /* IRQ22 */
    Default_Handler, /* IRQ23 */
    Default_Handler, /* IRQ24 */
    Default_Handler, /* IRQ25 */
    Default_Handler, /* IRQ26 */
    Default_Handler, /* IRQ27 */
    TIM2_IRQHandler, /* IRQ28 */
    Default_Handler, /* IRQ29 */
    Default_Handler, /* IRQ30 */
    Default_Handler, /* IRQ31 */
    Default_Handler, /* IRQ32 */
    Default_Handler, /* IRQ33 */
    Default_Handler, /* IRQ34 */
    Default_Handler, /* IRQ35 */
    Default_Handler, /* IRQ36 */
    Default_Handler, /* IRQ37 */
    Default_Handler, /* IRQ38 */
    Default_Handler, /* IRQ39 */
    Default_Handler, /* IRQ40 */
    Default_Handler, /* IRQ41 */
    Default_Handler, /* IRQ42 */
    Default_Handler, /* IRQ43 */
    Default_Handler, /* IRQ44 */
    Default_Handler, /* IRQ45 */
    Default_Handler, /* IRQ46 */
    Default_Handler, /* IRQ47 */
    Default_Handler, /* IRQ48 */
    Default_Handler, /* IRQ49 */
    Default_Handler, /* IRQ50 */
    Default_Handler, /* IRQ51 */
    Default_Handler, /* IRQ52 */
    Default_Handler, /* IRQ53 */
    Default_Handler, /* IRQ54 */
    Default_Handler, /* IRQ55 */
    Default_Handler, /* IRQ56 */
    Default_Handler, /* IRQ57 */
    Default_Handler, /* IRQ58 */
    Default_Handler, /* IRQ59 */
    Default_Handler, /* IRQ60 */
    Default_Handler, /* IRQ61 */
    Default_Handler, /* IRQ62 */
    Default_Handler, /* IRQ63 */
};

static void Default_Handler(void)
{
    while (1)
        ;
}

static void bss_zero(void)
{
    for (uint32_t *p = &_sbss; p < &_ebss; p++)
        *p = 0;
}

static void data_init(void)
{
    uint32_t *dst = &_sdata;
    const uint32_t *src = &_sidata;
    while (dst < &_edata)
        *dst++ = *src++;
}

void Reset_Handler(void)
{
    disable_irqs();
    disable_all_external_irqs();
    enable_fpu();
    bss_zero();
    data_init();
    (void)main();
    while (1)
        ;
}
