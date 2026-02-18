#ifndef SYSTEM_CONTROL_H
#define SYSTEM_CONTROL_H

#include <stdint.h>

void reboot_to_bootloader(void);
void reboot_to_app(void);
void request_bootloader_recovery(uint8_t long_press_mask);
void system_control_key_sequencer_init(uint32_t now_ms);
void system_control_key_sequencer_tick(uint32_t now_ms, uint8_t link_fault_active, uint8_t reboot_req);

#endif /* SYSTEM_CONTROL_H */
