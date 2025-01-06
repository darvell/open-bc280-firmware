#ifndef SYSTEM_CONTROL_H
#define SYSTEM_CONTROL_H

#include <stdint.h>

void reboot_to_bootloader(void);
void reboot_to_app(void);
void watchdog_tick(void);

#endif /* SYSTEM_CONTROL_H */
