#ifndef OPEN_FIRMWARE_PLATFORM_EARLY_INIT_H
#define OPEN_FIRMWARE_PLATFORM_EARLY_INIT_H

#include <stdint.h>

/* Minimal, no-SPI/no-bootlog early init helpers used by the boot monitor.
 *
 * Rationale: platform/board_init.c functions emit boot_stage_log(), which
 * touches SPI flash and can be slow/unavailable during bring-up. The boot
 * monitor wants BLE UART as early as possible with minimal dependencies.
 */

void platform_ble_control_pins_init_early(void);
void platform_uart1_pins_init_early(void);

/* Returns 1 if UART1 pins were configured in early init. Used to avoid
 * resetting USART1 again during full board init (which can drop the BLE UART
 * session right after 'continue boot'). */
uint8_t platform_uart1_was_inited_early(void);

#endif /* OPEN_FIRMWARE_PLATFORM_EARLY_INIT_H */

