#ifndef OPEN_FIRMWARE_PLATFORM_HW_H
#define OPEN_FIRMWARE_PLATFORM_HW_H

#include <stdint.h>

/* Cortex-M system control block */
#define SCB_VTOR 0xE000ED08u
#define SCB_AIRCR 0xE000ED0Cu
#define SCB_CFSR 0xE000ED28u
#define SCB_HFSR 0xE000ED2Cu
#define SCB_DFSR 0xE000ED30u
#define SCB_MMFAR 0xE000ED34u
#define SCB_BFAR 0xE000ED38u
#define SCB_AFSR 0xE000ED3Cu

#define SCB_AIRCR_VECTKEY (0x5FAu << 16)
#define SCB_AIRCR_SYSRESETREQ (1u << 2)

/* SysTick */
#define SYST_CSR 0xE000E010u
#define SYST_RVR 0xE000E014u
#define SYST_CVR 0xE000E018u

/* STM32F1/AT32F403A-style RCC (matches OEM image observations) */
#define RCC_BASE 0x40021000u
#define RCC_CR   (RCC_BASE + 0x00u)
#define RCC_CFGR (RCC_BASE + 0x04u)
#define RCC_CIR  (RCC_BASE + 0x08u)
#define RCC_APB2RSTR (RCC_BASE + 0x0Cu)
#define RCC_APB1RSTR (RCC_BASE + 0x10u)
#define RCC_AHBENR   (RCC_BASE + 0x14u)
#define RCC_APB2ENR  (RCC_BASE + 0x18u)
#define RCC_APB1ENR  (RCC_BASE + 0x1Cu)
#define RCC_CSR  (RCC_BASE + 0x24u)
#define RCC_MISC (RCC_BASE + 0x54u)
#define RCC_CSR_RMVF   (1u << 24)
#define RCC_CSR_BORRSTF (1u << 25)
#define RCC_CSR_PINRSTF (1u << 26)
#define RCC_CSR_PORRSTF (1u << 27)
#define RCC_CSR_SFTRSTF (1u << 28)
#define RCC_CSR_IWDGRSTF (1u << 29)
#define RCC_CSR_WWDGRSTF (1u << 30)
#define RCC_CSR_LPWRRSTF (1u << 31)

/* Watchdog (IWDG) */
#define IWDG_BASE 0x40003000u
#define IWDG_KR   (IWDG_BASE + 0x00u)
#define IWDG_PR   (IWDG_BASE + 0x04u)
#define IWDG_RLR  (IWDG_BASE + 0x08u)

/* Flash access control */
#define FLASH_ACR 0x40022000u

/* GPIO/UART/SPI (base addresses observed on BC280 platform) */
#define UART1_BASE 0x40013800u
#define UART2_BASE 0x40004400u
#define UART4_BASE 0x40004C00u
#define GPIOA_BASE 0x40010800u
#define GPIOB_BASE 0x40010C00u
#define GPIOC_BASE 0x40011000u
#define GPIOD_BASE 0x40011400u
#define GPIOE_BASE 0x40011800u
#define SPI1_BASE  0x40013000u

#define TIM1_BASE  0x40012C00u
#define TIM2_BASE  0x40000000u

#define GPIO_IDR(base) ((base) + 0x08u)
#define GPIO_CRL(base) ((base) + 0x00u)
#define GPIO_CRH(base) ((base) + 0x04u)
#define GPIO_ODR(base) ((base) + 0x0Cu)
#define GPIO_BSRR(base) ((base) + 0x10u)
#define GPIO_BRR(base)  ((base) + 0x14u)

#define UART_SR(o)  ((o) + 0x00u)
#define UART_DR(o)  ((o) + 0x04u)
#define UART_BRR(o) ((o) + 0x08u)
#define UART_CR1(o) ((o) + 0x0Cu)
#define UART_CR2(o) ((o) + 0x10u)
#define UART_CR3(o) ((o) + 0x14u)

#define TIM_CR1(base) ((base) + 0x00u)
#define TIM_DIER(base) ((base) + 0x0Cu)
#define TIM_SR(base) ((base) + 0x10u)
#define TIM_EGR(base) ((base) + 0x14u)
#define TIM_CCMR1(base) ((base) + 0x18u)
#define TIM_CCER(base) ((base) + 0x20u)
#define TIM_PSC(base) ((base) + 0x28u)
#define TIM_ARR(base) ((base) + 0x2Cu)
#define TIM_CNT(base) ((base) + 0x24u)
#define TIM_CCR1(base) ((base) + 0x34u)
#define TIM_BDTR(base) ((base) + 0x44u)

/* FSMC / EXMC */
#define FSMC_BASE 0xA0000000u
#define FSMC_BCR1  (FSMC_BASE + 0x00u)
#define FSMC_BTR1  (FSMC_BASE + 0x04u)
#define FSMC_BWTR1 (FSMC_BASE + 0x104u)

/* NVIC */
#define NVIC_ISER0 0xE000E100u
#define NVIC_ISER1 0xE000E104u
#define NVIC_IPR_BASE 0xE000E400u

#endif
