/**
 * @file stm32f1xx.h
 * @brief Compatibility shim for code expecting STM32 CMSIS headers
 *
 * This file redirects to the AT32F403A CMSIS headers which provide
 * the same Cortex-M4 intrinsics (__DMB, __DSB, __ISB, etc).
 */

#ifndef PLATFORM_STM32F1XX_H
#define PLATFORM_STM32F1XX_H

/* Pull in AT32 device header which includes core_cm4.h with CMSIS intrinsics */
#include "at32f403a_407.h"

#endif /* PLATFORM_STM32F1XX_H */
