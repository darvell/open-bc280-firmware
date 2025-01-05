#ifndef SYSTEM_CONTROL_H
#define SYSTEM_CONTROL_H

#include <stdint.h>

void reboot_to_bootloader(void);
void reboot_to_app(void);
void request_bootloader_recovery(uint8_t long_press_mask);
void watchdog_tick(void);

#endif /* SYSTEM_CONTROL_H */
