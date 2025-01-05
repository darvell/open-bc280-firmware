/**
 * @file at32f403a_407_conf.h
 * @brief AT32F403A/407 firmware library configuration for open-firmware
 *
 * This configuration file enables the peripheral drivers needed by the
 * open-firmware project running on the BC280 display hardware.
 */

#ifndef __AT32F403A_407_CONF_H
#define __AT32F403A_407_CONF_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Select the target device (AT32F403ARGT7 for BC280 hardware)
 * AT32F403ARGT7: 1024KB Flash, 96KB SRAM, LQFP64
 *
 * Note: Usually defined on command line, but provide default here
 */
#ifndef AT32F403ARGT7
#define AT32F403ARGT7
#endif

/**
 * @brief External high-speed oscillator (HEXT) frequency
 * BC280 uses 8MHz crystal
 */
#define HEXT_VALUE      ((uint32_t)8000000)

/**
 * @brief Internal high-speed oscillator (HICK) frequency
 */
#define HICK_VALUE      ((uint32_t)8000000)

/**
 * @brief HEXT startup timeout (in clock cycles)
 */
#define HEXT_STARTUP_TIMEOUT    ((uint16_t)0x3000)

/**
 * @brief Enable peripheral drivers used by open-firmware
 */
#define CRM_MODULE_ENABLED      /* Clock and Reset Management */
#define GPIO_MODULE_ENABLED     /* GPIO for buttons, LCD control, etc */
#define USART_MODULE_ENABLED    /* UART for motor comms and debug */
#define TMR_MODULE_ENABLED      /* Timers for PWM backlight, tick */
#define XMC_MODULE_ENABLED      /* FSMC/XMC for parallel LCD interface */
#define ADC_MODULE_ENABLED      /* ADC for battery voltage sensing */
#define FLASH_MODULE_ENABLED    /* Flash for config storage */
#define MISC_MODULE_ENABLED     /* Misc: NVIC, SysTick configuration */
#define SPI_MODULE_ENABLED      /* SPI for external flash (if used) */
#define DMA_MODULE_ENABLED      /* DMA for efficient transfers */
#define WDT_MODULE_ENABLED      /* Watchdog timer */
#define EXINT_MODULE_ENABLED    /* External interrupts */

/**
 * @brief Include device header
 */
#include "at32f403a_407.h"

/**
 * @brief Include enabled peripheral headers
 */
#ifdef CRM_MODULE_ENABLED
#include "at32f403a_407_crm.h"
#endif

#ifdef GPIO_MODULE_ENABLED
#include "at32f403a_407_gpio.h"
#endif

#ifdef USART_MODULE_ENABLED
#include "at32f403a_407_usart.h"
#endif

#ifdef TMR_MODULE_ENABLED
#include "at32f403a_407_tmr.h"
#endif

#ifdef XMC_MODULE_ENABLED
#include "at32f403a_407_xmc.h"
#endif

#ifdef ADC_MODULE_ENABLED
#include "at32f403a_407_adc.h"
#endif

#ifdef FLASH_MODULE_ENABLED
#include "at32f403a_407_flash.h"
#endif

#ifdef MISC_MODULE_ENABLED
#include "at32f403a_407_misc.h"
#endif

#ifdef SPI_MODULE_ENABLED
#include "at32f403a_407_spi.h"
#endif

#ifdef DMA_MODULE_ENABLED
#include "at32f403a_407_dma.h"
#endif

#ifdef WDT_MODULE_ENABLED
#include "at32f403a_407_wdt.h"
#endif

#ifdef EXINT_MODULE_ENABLED
#include "at32f403a_407_exint.h"
#endif

#ifdef __cplusplus
}
#endif

#endif /* __AT32F403A_407_CONF_H */
