/**
 * @file startup_at32f403a.c
 * @brief Startup code for AT32F403ARGT7 using Artery firmware library
 *
 * Provides:
 * - Vector table for Cortex-M4 and AT32F403A peripherals
 * - Reset handler with BSS zeroing and data initialization
 * - FPU enable
 * - Calls SystemInit() from Artery SDK then main()
 */

#include <stdint.h>

/* Linker-provided symbols */
extern uint32_t _stack_top;
extern uint32_t _sbss;
extern uint32_t _ebss;
extern uint32_t _sdata;
extern uint32_t _edata;
extern uint32_t _sidata;

/* Entry points */
int main(void);
extern void SystemInit(void);

/* Exception handlers - weak definitions allow override */
void Reset_Handler(void);
void NMI_Handler(void) __attribute__((weak, alias("Default_Handler")));
void HardFault_Handler(void);
void MemManage_Handler(void) __attribute__((weak, alias("Default_Handler")));
void BusFault_Handler(void) __attribute__((weak, alias("Default_Handler")));
void UsageFault_Handler(void) __attribute__((weak, alias("Default_Handler")));
void SVC_Handler(void) __attribute__((weak, alias("Default_Handler")));
void DebugMon_Handler(void) __attribute__((weak, alias("Default_Handler")));
void PendSV_Handler(void) __attribute__((weak, alias("Default_Handler")));
void SysTick_Handler(void);

/* AT32F403A peripheral interrupt handlers - weak definitions */
void WWDT_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void PVM_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void TAMPER_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void RTC_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void FLASH_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void CRM_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void EXINT0_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void EXINT1_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void EXINT2_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void EXINT3_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void EXINT4_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void DMA1_Channel1_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void DMA1_Channel2_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void DMA1_Channel3_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void DMA1_Channel4_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void DMA1_Channel5_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void DMA1_Channel6_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void DMA1_Channel7_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void ADC1_2_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void USBFS_H_CAN1_TX_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void USBFS_L_CAN1_RX0_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void CAN1_RX1_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void CAN1_SE_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void EXINT9_5_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void TMR1_BRK_TMR9_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void TMR1_OVF_TMR10_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void TMR1_TRG_HALL_TMR11_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void TMR1_CH_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void TMR2_GLOBAL_IRQHandler(void);  /* Used by time.c - defined there */
void TMR3_GLOBAL_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void TMR4_GLOBAL_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void I2C1_EVT_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void I2C1_ERR_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void I2C2_EVT_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void I2C2_ERR_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void SPI1_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void SPI2_I2S2EXT_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void USART1_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void USART2_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void USART3_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void EXINT15_10_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void RTCAlarm_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void USBFSWakeUp_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void TMR8_BRK_TMR12_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void TMR8_OVF_TMR13_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void TMR8_TRG_HALL_TMR14_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void TMR8_CH_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void ADC3_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void XMC_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void SDIO1_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void TMR5_GLOBAL_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void SPI3_I2S3EXT_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void UART4_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void UART5_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void TMR6_GLOBAL_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void TMR7_GLOBAL_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void DMA2_Channel1_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void DMA2_Channel2_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void DMA2_Channel3_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void DMA2_Channel4_5_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void SDIO2_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void I2C3_EVT_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void I2C3_ERR_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void SPI4_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));

/* Default handler for unused interrupts */
void Default_Handler(void)
{
    while (1)
        ;
}

/* Hard fault handler - weak, can be overridden in main.c */
void __attribute__((weak)) HardFault_Handler(void)
{
    while (1)
        ;
}

/**
 * @brief Vector table for AT32F403A
 * Located at start of flash (0x08010000 for app, or 0x08000000 for standalone)
 */
__attribute__((section(".isr_vector")))
void (*const g_isr_vector[])(void) = {
    /* Cortex-M4 core exceptions */
    (void (*)(void))(&_stack_top),  /* Initial stack pointer */
    Reset_Handler,                   /* Reset handler */
    NMI_Handler,                     /* NMI */
    HardFault_Handler,               /* Hard fault */
    MemManage_Handler,               /* Memory management fault */
    BusFault_Handler,                /* Bus fault */
    UsageFault_Handler,              /* Usage fault */
    0, 0, 0, 0,                      /* Reserved */
    SVC_Handler,                     /* SVCall */
    DebugMon_Handler,                /* Debug monitor */
    0,                               /* Reserved */
    PendSV_Handler,                  /* PendSV */
    SysTick_Handler,                 /* SysTick */

    /* AT32F403A peripheral interrupts (IRQ 0-63+) */
    WWDT_IRQHandler,                 /* 0: Window watchdog */
    PVM_IRQHandler,                  /* 1: PVM through EXINT */
    TAMPER_IRQHandler,               /* 2: Tamper */
    RTC_IRQHandler,                  /* 3: RTC global */
    FLASH_IRQHandler,                /* 4: Flash */
    CRM_IRQHandler,                  /* 5: CRM */
    EXINT0_IRQHandler,               /* 6: EXINT line 0 */
    EXINT1_IRQHandler,               /* 7: EXINT line 1 */
    EXINT2_IRQHandler,               /* 8: EXINT line 2 */
    EXINT3_IRQHandler,               /* 9: EXINT line 3 */
    EXINT4_IRQHandler,               /* 10: EXINT line 4 */
    DMA1_Channel1_IRQHandler,        /* 11: DMA1 channel 1 */
    DMA1_Channel2_IRQHandler,        /* 12: DMA1 channel 2 */
    DMA1_Channel3_IRQHandler,        /* 13: DMA1 channel 3 */
    DMA1_Channel4_IRQHandler,        /* 14: DMA1 channel 4 */
    DMA1_Channel5_IRQHandler,        /* 15: DMA1 channel 5 */
    DMA1_Channel6_IRQHandler,        /* 16: DMA1 channel 6 */
    DMA1_Channel7_IRQHandler,        /* 17: DMA1 channel 7 */
    ADC1_2_IRQHandler,               /* 18: ADC1 and ADC2 */
    USBFS_H_CAN1_TX_IRQHandler,      /* 19: USB high priority / CAN1 TX */
    USBFS_L_CAN1_RX0_IRQHandler,     /* 20: USB low priority / CAN1 RX0 */
    CAN1_RX1_IRQHandler,             /* 21: CAN1 RX1 */
    CAN1_SE_IRQHandler,              /* 22: CAN1 SE */
    EXINT9_5_IRQHandler,             /* 23: EXINT lines 5-9 */
    TMR1_BRK_TMR9_IRQHandler,        /* 24: TMR1 break / TMR9 */
    TMR1_OVF_TMR10_IRQHandler,       /* 25: TMR1 overflow / TMR10 */
    TMR1_TRG_HALL_TMR11_IRQHandler,  /* 26: TMR1 trigger+hall / TMR11 */
    TMR1_CH_IRQHandler,              /* 27: TMR1 channel */
    TMR2_GLOBAL_IRQHandler,          /* 28: TMR2 global */
    TMR3_GLOBAL_IRQHandler,          /* 29: TMR3 global */
    TMR4_GLOBAL_IRQHandler,          /* 30: TMR4 global */
    I2C1_EVT_IRQHandler,             /* 31: I2C1 event */
    I2C1_ERR_IRQHandler,             /* 32: I2C1 error */
    I2C2_EVT_IRQHandler,             /* 33: I2C2 event */
    I2C2_ERR_IRQHandler,             /* 34: I2C2 error */
    SPI1_IRQHandler,                 /* 35: SPI1 */
    SPI2_I2S2EXT_IRQHandler,         /* 36: SPI2 / I2S2EXT */
    USART1_IRQHandler,               /* 37: USART1 */
    USART2_IRQHandler,               /* 38: USART2 */
    USART3_IRQHandler,               /* 39: USART3 */
    EXINT15_10_IRQHandler,           /* 40: EXINT lines 10-15 */
    RTCAlarm_IRQHandler,             /* 41: RTC alarm */
    USBFSWakeUp_IRQHandler,          /* 42: USB wakeup */
    TMR8_BRK_TMR12_IRQHandler,       /* 43: TMR8 break / TMR12 */
    TMR8_OVF_TMR13_IRQHandler,       /* 44: TMR8 overflow / TMR13 */
    TMR8_TRG_HALL_TMR14_IRQHandler,  /* 45: TMR8 trigger+hall / TMR14 */
    TMR8_CH_IRQHandler,              /* 46: TMR8 channel */
    ADC3_IRQHandler,                 /* 47: ADC3 */
    XMC_IRQHandler,                  /* 48: XMC (FSMC) */
    SDIO1_IRQHandler,                /* 49: SDIO1 */
    TMR5_GLOBAL_IRQHandler,          /* 50: TMR5 */
    SPI3_I2S3EXT_IRQHandler,         /* 51: SPI3 / I2S3EXT */
    UART4_IRQHandler,                /* 52: UART4 */
    UART5_IRQHandler,                /* 53: UART5 */
    TMR6_GLOBAL_IRQHandler,          /* 54: TMR6 */
    TMR7_GLOBAL_IRQHandler,          /* 55: TMR7 */
    DMA2_Channel1_IRQHandler,        /* 56: DMA2 channel 1 */
    DMA2_Channel2_IRQHandler,        /* 57: DMA2 channel 2 */
    DMA2_Channel3_IRQHandler,        /* 58: DMA2 channel 3 */
    DMA2_Channel4_5_IRQHandler,      /* 59: DMA2 channels 4-5 */
    SDIO2_IRQHandler,                /* 60: SDIO2 */
    I2C3_EVT_IRQHandler,             /* 61: I2C3 event */
    I2C3_ERR_IRQHandler,             /* 62: I2C3 error */
    SPI4_IRQHandler,                 /* 63: SPI4 */
};

/* Helper functions */
static inline void disable_irqs(void)
{
    __asm__ volatile("cpsid i" ::: "memory");
}

static inline void enable_irqs(void)
{
    __asm__ volatile("cpsie i" ::: "memory");
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

/**
 * @brief Reset handler - entry point after reset
 */
void Reset_Handler(void)
{
    disable_irqs();

    /* Zero BSS and copy initialized data */
    bss_zero();
    data_init();

    /* Call Artery SDK SystemInit (enables FPU, resets CRM to known state) */
    SystemInit();

    /*
     * Do NOT enable interrupts here - main() handles this after
     * hardware initialization is complete. Enabling early creates
     * a race where TIM2 or other IRQs could fire before handlers
     * are ready.
     */
    (void)main();

    /* Should never return */
    while (1)
        ;
}
