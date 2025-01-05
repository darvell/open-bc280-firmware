#ifndef OPEN_FIRMWARE_PLATFORM_BOARD_INIT_H
#define OPEN_FIRMWARE_PLATFORM_BOARD_INIT_H

#include <stdint.h>

void platform_nvic_init(void);
void platform_board_init(void);
void platform_uart_irq_init(void);
void platform_uart_pins_init(void);
void platform_buttons_init(void);
void platform_gpioc_aux_init(void);
void platform_ble_control_pins_init(void);

#endif
