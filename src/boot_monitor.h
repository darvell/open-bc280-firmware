#ifndef OPEN_FIRMWARE_BOOT_MONITOR_H
#define OPEN_FIRMWARE_BOOT_MONITOR_H

#include <stddef.h>
#include <stdint.h>

/* Boot monitor: minimal early environment over BLE UART (UART1) that waits
 * for an explicit 'continue boot' command before initializing the full stack.
 */

void boot_monitor_run(void);

void boot_monitor_request_continue(void);
uint8_t boot_monitor_should_continue(void);
void boot_monitor_clear_continue(void);

/* Build the monitor-info payload (response to CMD 0x09 with LEN=0).
 * Returns payload length (0 on error). */
uint8_t boot_monitor_build_info(uint8_t *out, uint8_t cap);

#endif /* OPEN_FIRMWARE_BOOT_MONITOR_H */

