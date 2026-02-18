# OEM Init Order (BC280 JH0 App v2.2.8)

Source DB: `BC280_Combined_Firmware_0.2.0_2.2.8-JH0.bin` (boot + app)

This note captures the boot-critical app startup sequence and MMIO behavior from IDA so open-firmware can match the hardware bring-up order that avoids early boot faults/hangs.

## App entry and top-level order

App reset vector:
- `0x08010004` -> `0x08010191` (Thumb)
- startup stub jumps to `app_reset_init_and_mainloop` (`0x8025E7A`, renamed in IDA)

Observed init order in `app_reset_init_and_mainloop`:
1. `init_key_pb1_low` (`0x801B838`)
2. `init_clock_nvic_vtor` (`0x802035A`)
3. `init_soft_timer_slots` (`0x8020A84`)
4. `spi1_flash_hw_init` (`0x801DE98`)
5. `lcd_backlight_adc_init` (`0x8019918`)
6. `ble_uart1_stack_init` (`0x80128DC`)
7. `ble_protocol_init_and_timer` (`0x80117DC`)
8. config/UI/power helpers (`sub_8018BAC`, `sub_801B380`, `sub_801B554`, `APP_Dispatcher_Init`)
9. buttons init (`sub_801934C`)
10. `tick_timers_init`, `rtc_init`, `battery_monitor_init` (`0x80109D0`), power-state helper (`sub_801D1BC`)
11. `__enable_irq()`, then forever loop.

## Boot-critical hardware steps

### 1) PB1 KEY low at boot
- Function: `init_key_pb1_low` (`0x801B838`)
- Behavior:
  - enables GPIOB clock
  - configures `PB1` as output
  - drives PB1 low via BRR path (`sub_8018E14`)

### 2) Clock tree + AIRCR + VTOR
- Function: `init_clock_nvic_vtor` (`0x802035A`)
- Calls:
  - `rcc_clock_tree_init` (`0x801CD64`)
  - `systemcoreclock_update_from_rcc` (`0x802081C`)
  - `sub_801A686` -> writes `AIRCR = 0x05FA0500` (priority grouping)
  - `sub_801A74C` with `R0=0x08000000`, `R1=0x00010000` -> writes `VTOR=0x08010000`

### 3) TIM2 5ms timebase and scheduler slots
- Functions:
  - `init_soft_timer_slots` (`0x8020A84`)
  - `tim2_timebase_init_5ms` (`0x8020E98`)
- Behavior:
  - clears timer-slot arrays (24 entries)
  - enables TIM2 clock/reset
  - programs `PSC=35999`, `ARR=9` at base `0x40000000`
  - configures NVIC IRQ28
  - many OEM periodic tasks use units of `5ms` ticks (`sub_802090C` stores `period_ms / 5`)

### 4) SPI1 flash interface
- Function: `spi1_flash_hw_init` (`0x801DE98`)
- GPIOA config:
  - `PA5/PA7` AF output
  - `PA6` input pull-up
  - `PA4` output (CS)
- Peripheral:
  - enables SPI1 (`0x40013000`) clock/reset path
  - programs SPI1 control registers via `sub_801DCCC`/`sub_801DD42`

### 5) LCD/FSMC + ST7789 + backlight/ADC
- Function: `lcd_backlight_adc_init` (`0x8019918`)
- Order:
  - `lcd_fsmc_gpio_init` (`0x8019420`)
  - `fsmc_bank1_timing_init` (`0x801987C`) enabling FSMC block
  - `st7789_init_240x240` (`0x80196BC`) on `0x60000000/0x60020000`
  - framebuffer clear helper
  - `adc1_backlight_init` (`0x8011044`) for TIM1/PA8 and ADC1 setup

### 6) BLE control pins and UART1
- Functions:
  - `ble_control_pins_init` (`0x80111E4`)
  - `usart1_init_9600_irq` (`0x80127FC`)
  - `reset_ble_uart_buffers` (`0x80127B8`)
- Strap states from `ble_control_pins_init`:
  - `PA12` high
  - `PA11` low
  - `PC12` low
- UART1:
  - `PA9` TX, `PA10` RX pull-up
  - `9600` baud path
  - NVIC IRQ37 enabled

### 7) Motor UART2 init path (runtime comms)
- Function: `motor_uart2_init_9600_irq` (`0x8020FF0`)
- Behavior:
  - `PA2` TX AF, `PA3` RX pull-up
  - USART2 @ `0x40004400`
  - `9600` baud
  - NVIC IRQ38 enabled

## Notes on PB1 high timing

In this JH0 image, PB1 low/high transitions are driven by state machine calls inside `sub_801EEF4`:
- case `1`: set PB1 high (`sub_8018E18(0x40010C00, 2)`)
- case `6`: set PB1 low (`sub_8018E14(0x40010C00, 2)`)

This matches the pattern "hold low during early init, assert high later under scheduler control".

## Open-firmware parity checklist (boot-critical)

When validating open-firmware startup against this JH0 image, keep these as required:
1. PB1 low before peripheral bring-up.
2. RCC/PLL + AIRCR grouping + VTOR set to app vectors before enabling IRQs.
3. TIM2 5ms base (`PSC=35999`, `ARR=9`) active before code that relies on timeout progression.
4. SPI1 flash GPIO/peripheral config aligned to PA4..PA7 mapping.
5. LCD/FSMC init before UI draw traffic.
6. BLE straps (`PA12 high`, `PA11 low`, `PC12 low`) before UART1 traffic.
7. USART1/USART2 IRQ priorities and enable state set before runtime loops consume RX.

## Current unknowns (not boot blockers)

- Full semantic mapping of all UI state machine cases in `sub_801EEF4`.
- Full command-level equivalence for non-default motor protocols.
- Complete RTC persistence behavior.
