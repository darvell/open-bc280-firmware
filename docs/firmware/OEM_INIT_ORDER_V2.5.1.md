---
title: OEM Init Order (BC280 App v2.5.1)
status: draft
oem_binary: B_JH_FW_APP_DT_BC280_V2.5.1.bin
---

# OEM Init Order (BC280 App v2.5.1)

This document captures the *observed* OEM initialization order from the v2.5.1
application, with evidence anchors (IDA addresses / MMIO bases), and maps each
step to the closest open-firmware equivalent.

Scope notes:
- This is about **hardware interaction and sequencing**, not UI semantics.
- OEM function names are treated as hints only; the evidence is the **MMIO** they touch.

## OEM Top-Level Entry

The OEM application main entrypoint is `sub_8026266` @ `0x8026266` (noreturn).
It runs with IRQs disabled, performs init, then enables IRQs and enters a forever loop.

Init call order (as decompiled) from `sub_8026266`:

1. `sub_801BC2C` (PB1 KEY output init, drive low)
2. `sub_802075E` (system clock init via `clock_system_init`)
3. `sub_8020E88` (scheduler tables + TIM2 timebase init)
4. `sub_801E288` (SPI1 init, flash chip-select high)
5. `sub_8019CEC` (LCD FSMC + ST7789 init + backlight TIM1 init + ADC init)
6. `sub_80128E4` (BLE control pins + UART1 init + buffer reset)
7. `ble_protocol_init_and_timer` @ `0x80117E0` (register BLE/Motor callbacks, schedule periodic)
8. `sub_8018F80` (PC5/PC6 OD outputs)
9. `sub_801B774` (config/persistence init, schedule periodic tasks)
10. `sub_801B948` (motor UART/protocol selection init)
11. `APP_Dispatcher_Init` (UI dispatcher/vtable init)
12. `init_gpio_and_scheduler` @ `0x8019720` (PC0-4 button IO, event system init)
13. `tick_timers_init` @ `0x801FFE4` (schedule `sub_801F2E4(0)` after ~10ms)
14. `rtc_init` + follow-on runtime init helpers
15. `__enable_irq()`

## Hardware-Relevant Details Per Step

### 1) PB1 KEY output init (drive low)

OEM:
- `sub_801BC2C` @ `0x801BC2C`
- GPIOB base: `0x40010C00`
- Configures PB1 as output and writes GPIOB_BRR (offset `0x14`) with `0x0002` (drive low).

Open-firmware match:
- `src/main.c` `platform_power_hold_early()` (very early PB1 low)
- `platform/board_init.c` `platform_key_output_set(0)`

### 2) System clocks

OEM:
- `sub_802075E` calls `clock_system_init` @ `0x801D154` (RCC reset, HSE/PLL, flash wait states).

Open-firmware match:
- `platform/clock.c` `platform_clock_init()`

### 3) TIM2 timebase init (5ms tick)

OEM:
- `sub_8020E88` calls `sub_8020EE8` which calls `APP_init_timer2_pwm(ARR=9, PSC=35999)` @ `0x802129C`.
- TIM2 base: `0x40000000`
- NVIC IRQ 28 configured here (same IRQ as our TIM2 handler).

Open-firmware match:
- `platform/time.c` `platform_timebase_init_oem()` (PSC=35999, ARR=9, IRQ28)

### 4) SPI1 init (external flash)

OEM:
- `sub_801E288` configures GPIOA for SPI1 pins and sets CS high (PA4 via BSRR with mask `0x10`).
- Enables SPI1 clock (bit `0x1000`) and configures SPI1 base `0x40013000`.

Open-firmware match:
- `drivers/spi_flash.c` SPI1 init (PA4..PA7)
- Early use: `drivers/spi_flash.c` `spi_flash_set_bootloader_mode_flag()` is called early in `src/main.c`.

### 5) LCD + Backlight + ADC

OEM:
- `sub_8019CEC`:
  - Calls `sub_8019C50` which writes FSMC timing/registers.
  - Uses 8080 MMIO mapping (`0x60000000` cmd, `0x60020000` data).
  - Calls `st7789_init_240x240()` and clears screen.
  - Calls `APP_init_motor_control_adc` @ `0x8011048`:
    - Configures PA8 for TIM1 CH1 PWM (TIM1 base `0x40012C00`).
    - Initializes backlight to 0 via `display_set_backlight_level(0)`.

Open-firmware match:
- `platform/board_init.c`:
  - `platform_lcd_bus_pins_init()`, `platform_fsmc_init()`, `platform_lcd_init_oem_8080()`
  - `platform_backlight_init(0)`
  - `platform_adc_init()`

### 6) BLE module control pins + UART1 init

OEM:
- `sub_80128E4`:
  - `sub_80111E8`: configures PA11/PA12 outputs and drives PA12 high / PA11 low; configures PC12 and drives it low.
  - `sub_8012804(9600)`: configures PA9/PA10 and initializes USART1 @ `0x40013800`, including IRQ37 configuration.

Open-firmware match:
- `platform/board_init.c`:
  - `platform_ble_control_pins_init()` (PA11/PA12/PC12 straps + PA12 reset pulse)
  - `platform_ble_uart_pins_init()` (PA9/PA10, USART1 clock/reset handling)
- `src/main.c` then initializes USART1 registers via `uart_init_basic(UART1_BASE, ...)`.

### 7) BLE protocol registration + periodic

OEM:
- `ble_protocol_init_and_timer` @ `0x80117E0` registers callbacks and schedules a periodic task after ~200ms.

Open-firmware match:
- `src/comm/comm.c` and BLE framing logic, plus main loop `poll_uart_rx_ports()`.

### 8) PC5/PC6 OD outputs

OEM:
- `sub_8018F40` configures PC5+PC6 as GPIO output open-drain (mode nibble `0x5`) and sets their ODR bits high
  (released/high for open-drain).

Open-firmware match:
- `platform/board_init.c` `platform_gpioc_aux_init()` (config + sets ODR bits high)
- In open-firmware this is called after UART1 init in `src/main.c` to better match OEM timing.

### 9) Config/persistence init

OEM:
- `sub_801B774` schedules long-period timers and computes derived values from persisted config.

Open-firmware match:
- `src/config/config.c` + `storage/*` init in `src/main.c` (config load, logs, AB init).

### 10) Motor UART init/protocol selection

OEM:
- `sub_801B948` chooses a motor protocol and calls a UART2 init helper (USART2 base `0x40004400`).

Open-firmware match:
- `platform/board_init.c` `platform_motor_uart_pins_init()` (PA2/PA3 + USART2 reset)
- `src/main.c` then configures USART2 registers via `uart_init_basic(UART2_BASE, ...)` and enables motor ISR logic.

### 11-13) UI dispatcher, button event system, and power state machine scheduling

OEM:
- `APP_Dispatcher_Init()` initializes large UI/dispatcher tables.
- `init_gpio_and_scheduler()` configures PC0-4 and sets up the button event system.
- `tick_timers_init()` schedules `sub_801F2E4(0)` after 10ms, which schedules case 1 that asserts PB1 high.

Open-firmware match:
- UI init: `ui/` + `ui_init(&g_ui)` in `src/main.c`
- Button FSM: `src/input/*` + `buttons_tick()`
- PB1 assert timing: `src/main.c` delays ~10ms of TIM2 ticks after IRQ enable, then `platform_key_output_set(1)`.

## Open-Firmware Init Order (Current, For Comparison)

This is the current open-firmware init sequence (high-level) with file anchors:

1. Very early hold/key defaults:
   - `src/main.c` `platform_power_hold_early()` drives PB1 low and forces PA8 low.
2. Clocks + NVIC + TIM2 timebase:
   - `platform_clock_init()`, `platform_nvic_init()`, `platform_timebase_init_oem()` in `src/main.c`.
3. SPI flash early use:
   - `spi_flash_set_bootloader_mode_flag()` in `src/main.c` (forces SPI1 init by first use).
4. Safe-mode sampling:
   - `platform_buttons_init()` then read `GPIOC_IDR` in `src/main.c`.
5. Board init (LCD, backlight, ADC, BLE pins, UART1 pins, buttons):
   - `platform_board_init()` in `platform/board_init.c`.
6. UART1 register init:
   - `uart_init_basic(UART1_BASE, ...)` in `src/main.c`.
7. PC5/PC6 OD outputs (moved later to match OEM timing):
   - `platform_gpioc_aux_init()` in `src/main.c`.
8. UART2 pin/reset + register init:
   - `platform_motor_uart_pins_init()` then `uart_init_basic(UART2_BASE, ...)` in `src/main.c`.
9. Motor ISR + protocol init + config/UI init:
   - `motor_isr_init`, `motor_cmd_init`, `shengyi_init`, `config_load_active`, `ui_init` in `src/main.c`.
10. PB1 assert timing:
   - After IRQ enable and ~10ms wait, `platform_key_output_set(1)` in `src/main.c`.
