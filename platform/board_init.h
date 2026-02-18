#ifndef OPEN_FIRMWARE_PLATFORM_BOARD_INIT_H
#define OPEN_FIRMWARE_PLATFORM_BOARD_INIT_H

#include <stdint.h>

void platform_nvic_init(void);
void platform_board_init(void);
void platform_uart_irq_init(void);
void platform_ble_uart_pins_init(void);
void platform_motor_uart_pins_init(void);
void platform_uart_pins_init(void);
void platform_buttons_init(void);
void platform_gpioc_aux_init(void);
void platform_ble_control_pins_init(void);

/* PB1 "KEY"/enable output.
 * OEM app v2.5.1 drives this low during early init, then high once running,
 * and low again during shutdown. */
void platform_key_output_set(uint8_t on);

/* BLE module control pins (OEM wiring assumptions for BC280-derived harnesses).
 *
 * These are mainly for bring-up/diagnostics; normal boot just calls
 * platform_ble_control_pins_init().
 */
void platform_ble_pa11_set(uint8_t high);
void platform_ble_pa12_set(uint8_t high);
void platform_ble_pc12_set(uint8_t high);
uint8_t platform_ble_pa11_get(void);
uint8_t platform_ble_pa12_get(void);
uint8_t platform_ble_pc12_get(void);
void platform_ble_reset_pulse(uint32_t low_ms);

/* Backlight control (TIM1 CH1 on PA8). Levels match OEM semantics: 0..5 => 0..100% in 20% steps. */
void platform_backlight_set_level(uint8_t level);

#endif
