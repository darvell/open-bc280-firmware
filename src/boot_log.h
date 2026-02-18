#ifndef OPEN_FIRMWARE_BOOT_LOG_H
#define OPEN_FIRMWARE_BOOT_LOG_H

#include <stdint.h>

void boot_log_stage(uint32_t code);
void boot_log_uart_ready(void);
void boot_log_lcd_ready(void);

#endif
