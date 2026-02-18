---
title: OEM Pin / Peripheral Map (IDA) - BC280 App v2.5.1
status: draft
source: IDA (B_JH_FW_APP_DT_BC280_V2.5.1.bin)
---

# OEM Pin / Peripheral Map (IDA) - BC280 App v2.5.1

This is a hardware-facing map extracted from the OEM application firmware using IDA (via `mcporter`).

Notes:
- Function names in the IDA DB may be wrong. This map is anchored primarily on **MMIO addresses**, **GPIO pin masks**,
  and **register offsets**, not on names.
- This document is for the **active IDA instance** `B_JH_FW_APP_DT_BC280_V2.5.1.bin`. Newer combined images
  (ex: `firmware/BC280_Combined_Firmware_3.3.6_4.2.5.bin`) should be re-verified in their own IDA DB.

## Pin Summary (Quick Table)

This is the condensed "what goes where" map for the v2.5.1 app build. Details and evidence are in the sections below.

| Pin | Direction / Mode (OEM) | Likely Function | Evidence Anchor |
|---|---|---|---|
| PA0 | analog in | battery ADC (ADC1) | `sub_8010C40` |
| PA2 / PA3 | AF out / input PU | motor UART (USART2 TX/RX) | `sub_80213F4` |
| PA4..PA7 | GPIO out / AF out / input PU | SPI1 flash (CS/SCK/MISO/MOSI) | `sub_801E288` |
| PA8 | AF out (two-phase config) | backlight PWM (TIM1 CH1) | `sub_80197F4`, `APP_init_motor_control_adc` |
| PA9 / PA10 | AF out / input PU | BLE UART (USART1 TX/RX) | `sub_8012804` |
| PA11 / PA12 / PC12 | GPIO out | BLE module control straps (EN/RESET/BOOT?) | `sub_80111E8`, `ble_disconnect_active_connection` |
| PB0 | GPIO out | LCD reset | `st7789_init_240x240` |
| PB1 | GPIO out | KEY / power-hold output | `sub_801BC2C`, `sub_801F2E4` |
| PC0..PC4 | input PU | button sampling | `init_gpio_and_scheduler`, `sub_80196EC` |
| PC5 / PC6 | output OD (released high) | unknown harness IO / bus lines | `sub_8018F40` |

## GPIO Config Helper (How Pins Are Set Up)

Most pin init routes through `gpio_configure_pins_with_mode` @ `0x8019146`.

Call pattern (as observed in multiple init functions):
- Stack struct at `&mask16`:
  - `uint16_t mask` at offset `+0`
  - `uint8_t mode_lo` at offset `+2`
  - `uint8_t flags_mode_hi` at offset `+3`
- Port base passed as an `int *` like `0x40010800` (GPIOA), `0x40010C00` (GPIOB), etc.

What it does (evidence: decompile @ `0x8019146`):
- Updates `GPIOx_CRL` (pins 0..7) and `GPIOx_CRH` (pins 8..15) using a 4-bit field per pin.
- If `flags_mode_hi == 0x48` (decimal 72), it also writes the pin mask to `GPIOx_BSRR` to force a "1" in the ODR
  bit (used for input pull-up config).
- If `flags_mode_hi == 0x28` (decimal 40), it writes the pin mask to `GPIOx_BRR` to force a "0" in the ODR bit.

Important nuance:
- For pull-up / pull-down configs (`flags_mode_hi` of `0x48` / `0x28`), the OEM sets the 4-bit mode nibble to `0x8`
  (input with pull-up/down) by passing an `extend` value of `0`. If `extend` were non-zero, the OR-composition would
  change the nibble away from `0x8` and the pin would no longer be an input.

Naming / porting footgun:
- In various OEM-derived notes, the extra value ORed into the 4-bit CRL/CRH nibble gets called `extend`.
- Practically, it behaves like the STM32F1-style “MODE” speed bits for output/AF pins (and must be **zero** for the
  `0x48`/`0x28` pull-up/down input cases).
- This matters for pins like PA8 where the OEM config is two-phase and the output-speed setting should be preserved.

Interpreting the 4-bit per-pin config matches STM32F1-style `CNF/MODE` encoding (strong evidence: input pull-up
implementation via ODR, use of `CRL/CRH`, use of `BSRR/BRR`).

## LCD Panel (8080-style parallel via FSMC)

### Memory-mapped interface
- Command register: `*(volatile uint16_t *)0x60000000`
- Data register: `*(volatile uint16_t *)0x60020000`
- Evidence: direct writes in `st7789_init_240x240` @ `0x8019A90`, draw ops like `LCD_fill_rectangle` @ `0x80198B4`.

This is a classic FSMC 8080-style mapping where `A16` (physical) is used as D/C:
- `0x60000000` vs `0x60020000` differs by `0x20000` which corresponds to an address line on a 16-bit bus.

### FSMC enable/config
- AHB clock bit set via `sub_801CF40(0x100, 1)` which ORs `0x100` into `RCC_AHBENR` @ `0x40021014`
  - Evidence: `sub_8019C50` @ `0x8019C50` calls `sub_801CF40(256)`
- FSMC register programming via generic table writer `sub_8018FD4` @ `0x8018FD4`
  - Writes to `0xA0000000 + (4 * bank_index)` derived from `4 * *a1 - 0x60000000`
  - Evidence: `sub_8018FB4` disasm @ `0x8018FB4` uses `R0 = (idx<<2) - 0x60000000` -> `0xA0000000` when `idx==0`.

### LCD GPIO pin assignment (bus + control)

`sub_80197F4` @ `0x80197F4` configures the LCD-related pins:
- GPIOA: mask `0x0100` (PA8) configured AF (later reconfigured explicitly for PWM)
- GPIOB: mask `0x0001` (PB0) configured output (used as LCD reset)
- GPIOD: mask `0xCFB3` (PD0, PD1, PD4, PD5, PD7, PD8, PD9, PD10, PD11, PD14, PD15) configured AF
- GPIOE: mask `0xFF80` (PE7..PE15) configured AF
- Evidence: decompile `sub_80197F4` @ `0x80197F4`, plus mask decoding:
  - `0xCFB3` => pins `[0,1,4,5,7,8,9,10,11,14,15]`
  - `0xFF80` => pins `[7,8,9,10,11,12,13,14,15]`

Mapping these to a typical FSMC 16-bit bus on STM32F1-like parts:
- Data bus:
  - PD14 = D0
  - PD15 = D1
  - PD0  = D2
  - PD1  = D3
  - PE7  = D4
  - PE8  = D5
  - PE9  = D6
  - PE10 = D7
  - PE11 = D8
  - PE12 = D9
  - PE13 = D10
  - PE14 = D11
  - PE15 = D12
  - PD8  = D13
  - PD9  = D14
  - PD10 = D15
- Control:
  - PD4  = FSMC_NOE (RD)
  - PD5  = FSMC_NWE (WR)
  - PD7  = FSMC_NE1 (CS)
  - PD11 = FSMC_A16 (D/C select; maps to `0x60000000` vs `0x60020000`)

### LCD reset pin
- PB0 is toggled via `GPIOB_BSRR/BRR`:
  - `sub_80191EC(GPIOB, 1)` writes `1` to `GPIOB_BSRR` (set PB0)
  - `sub_80191E8(GPIOB, 1)` writes `1` to `GPIOB_BRR`  (reset PB0)
- Evidence:
  - `st7789_init_240x240` @ `0x8019A90` calls `sub_80191EC(0x40010C00, 1)` and `sub_80191E8(0x40010C00, 1)`

## Backlight PWM (TIM1 on PA8)

The backlight is driven by TIM1:
- TIM base: `0x40012C00`
- PWM duty writes: `*(uint32_t *)(TIM1 + 0x34)` (offset 52) via `sub_8021218(TIM1, duty)`
  - Evidence: `sub_8021218` @ `0x8021218`
- Output enable: sets/clears bit `0x8000` at `*(uint16_t *)(TIM1 + 0x44)` via `sub_802102C(TIM1, enable)`
  - Evidence: `sub_802102C` @ `0x802102C` (matches TIM1 BDTR.MOE semantics on STM32F1-like timers)
- PWM output pin: PA8 configured AF output before enabling timer
  - Evidence: `APP_init_motor_control_adc` @ `0x8011048` configures GPIOA mask `0x0100` with AF-style nibble value,
    then initializes TIM1 and calls `display_set_backlight_level(0)`

PA8 init nuance (two-phase):
- PA8 is configured once as part of the early LCD/FSMC pin group init (`sub_80197F4`), and then configured again
  later when TIM1 PWM is set up (`APP_init_motor_control_adc`).
- When mirroring OEM behavior, treat PA8 as “owned” by backlight PWM but expect it to be touched in multiple init phases.

Brightness levels:
- `display_set_backlight_level` @ `0x80110F4` uses discrete duty values: `0,20,40,60,80,100`.

## SPI Flash (SPI1 on PA4..PA7)

SPI1 is used for external flash:
- SPI base: `0x40013000` (SPI1)
- GPIO config for SPI pins: `sub_801E288` @ `0x801E288`
  - PA5 + PA7 (mask `0x00A0`) set to AF output (SCK, MOSI)
  - PA6 (mask `0x0040`) set to input pull-up (MISO)
  - PA4 (mask `0x0010`) set to output (CS), then driven high via `GPIOA_BSRR`

Runtime behavior (CS toggling):
- PA4 is not a “set once then forget” output; it is toggled at runtime for flash transactions.
- In v2.5.1, the same BSRR/BRR helper pattern used for other GPIO outputs is also used to assert/deassert PA4
  around SPI transfers (manual chip-select rather than hardware NSS).

Pin map:
- PA4 = SPI1_NSS (manual CS)
- PA5 = SPI1_SCK
- PA6 = SPI1_MISO (pull-up)
- PA7 = SPI1_MOSI

## BLE UART (USART1 on PA9/PA10)

BLE serial transport uses USART1:
- USART1 base: `0x40013800`
- Init sequence: `sub_8012804` @ `0x8012804`
  - PA9 (mask `0x0200`) configured AF output (USART1_TX)
  - PA10 (mask `0x0400`) configured input pull-up (USART1_RX)
  - NVIC config uses IRQ number `37` (consistent with USART1 IRQ)

Additional BLE control pins (needs confirmation of semantics, but the pin actions are real):
- `sub_80111E8` @ `0x80111E8`:
  - Configures PA11 + PA12 (mask `0x1800`) as outputs.
  - Sets PA12 high (`GPIOA_BSRR` with `0x1000`) and drives PA11 low (`GPIOA_BRR` with `0x0800`).
  - Configures PC12 (mask `0x1000`) as output, then drives it low (`GPIOC_BRR` with `0x1000`).
  - Hypothesis: these are BLE module `EN`, `RESET`, `BOOT` (or similar strap pins).
- `ble_disconnect_active_connection` @ `0x8011254`:
  - Drives PA12 low, delays 10ms, drives PA12 high, then drives PA11 low.
  - Strong evidence: PA12 is a BLE module reset line (active-low), PA11 is a strap/enable line that the OEM keeps low
    at least during reset/disconnect handling. PC12 is also held low at init and not otherwise toggled in this build.

PC5/PC6 open-drain outputs:
- `sub_8018F40` @ `0x8018F40` configures PC5 + PC6 (mask `0x0060`) as **GPIO output open-drain** (mode nibble `0x5`,
  STM32F1-style 10MHz OD).
- It then sets the **ODR bits high** for PC5/PC6 by writing to GPIOC_BSRR (via `sub_80191F0(GPIOC, 0x20, 1)` and
  `sub_80191F0(GPIOC, 0x40, 1)`).
  - For open-drain outputs, ODR=1 means the pin is released (hi-Z) rather than actively driven high.
- Follow-up search in this v2.5.1 app build did not find any later reads/writes of PC5/PC6 (no additional callsites
  driving GPIOC bit masks `0x20`/`0x40` via the BSRR/BRR helper), so these lines appear to be left in the released
  state for the remainder of runtime. The likely intent is harness accessory IO or a bus that is externally pulled up.

## Power / Key Output (GPIOB PB1)

PB1 is configured as a GPIO output early in init and then toggled by the UI state machine:
- Config + default low:
  - `sub_801BC2C` @ `0x801BC2C` enables GPIOB clock, configures PB1 (mask `0x0002`) as output, then drives it low
    via `GPIOB_BRR` (`sub_80191E8(0x40010C00, 2)`).
- Asserted during normal run:
  - `sub_801F2E4` (UI/power state machine) case `1` calls `sub_80191EC(0x40010C00, 2)` (PB1 high).
- Deasserted during shutdown:
  - `sub_801F2E4` case `6` calls `sub_80191E8(0x40010C00, 2)` (PB1 low) right before blanking the screen and
    turning off the backlight.

OEM sequencing detail (evidence-backed):
- The OEM does *not* drive PB1 high immediately after configuring it.
- `sub_8026266` @ `0x8026266` (main loop entry) calls `sub_801BC2C()` early with IRQs disabled, then later calls
  `tick_timers_init()` @ `0x801FFE4` (still with IRQs disabled).
- `tick_timers_init` schedules `sub_801F2E4(0)` after `0x0A` (10ms) via `sub_8020D10(2, 0x0A, sub_801F2E4, 0, ...)`.
- `sub_801F2E4(0)` (case 0) then registers/schedules `sub_801F2E4(1)`, and `sub_801F2E4(1)` (case 1) sets PB1 high.

Interpretation (hypothesis, but consistent with behavior):
- PB1 is very likely the "KEY"/"SW" output that enables or latches external power (common on e-bike controller harnesses).
  It is not toggled like a buzzer; it is treated like a persistent enable.

## Motor Controller UART (USART2 on PA2/PA3)

Motor controller comms use USART2:
- USART2 base: `0x40004400`
- Init sequence: `sub_80213F4` @ `0x80213F4`
  - PA2 (mask `0x0004`) configured AF output (USART2_TX)
  - PA3 (mask `0x0008`) configured input pull-up (USART2_RX)
  - APB1 clock enable: sets bit `1<<17` in `RCC_APB1ENR` @ `0x4002101C` via `sub_801CF58`
  - APB1 reset toggled via `sub_801CF70`
  - NVIC config uses IRQ number `38` (consistent with USART2 IRQ)

### Motor protocol frame format (Shengyi DWG22, 0x3A-framed, CRLF-terminated)

Evidence sources:
- Parser: `tongsheng_uart_packet_parser` @ `0x8025068` (IDA symbol prefix is a misnomer; this is Shengyi DWG22)
- Checksum: `validate_tongsheng_packet_checksum` @ `0x8024084`
- Builder finalizer: `ui_cmd_finalize_enqueue` @ `0x8025600`

Frame layout (as built by `ui_cmd_finalize_enqueue`):
- Byte 0: `0x3A` (SOF)
- Byte 1: second header byte (typically `0x1A` in builders; OEM RX logic does not appear to enforce a fixed value)
- Byte 2: `cmd`
- Byte 3: `payload_len` (set as `len_before_finalize - 4`)
- Byte 4..(4 + payload_len - 1): payload bytes (0..142 bytes)
- Next 2 bytes: 16-bit checksum, little-endian
  - checksum = `sum(frame[1..(end_of_payload)])` (SOF excluded, payload included)
- Last 2 bytes: `0x0D 0x0A` (CR LF)

Checksum verification (`validate_tongsheng_packet_checksum` @ `0x8024084`):
- Sums the received bytes excluding SOF and excluding the last 2 checksum bytes.
- Compares against `uint16_t` stored in the last 2 bytes before CRLF.

Examples of response builders:
- `build_tongsheng_empty_response_packet` @ `0x8025214`: `3A 1A <cmd> 00 ...`
- `build_tongsheng_response_packet` @ `0x8025236`: `3A 1A <cmd> 01 <param> ...`
- `build_tongsheng_response_packet_u32be` @ `0x8025488`: payload is 4 bytes big-endian.

Naming note:
- Many OEM/IDA artifacts call this subsystem “tongsheng”. On BC280/Aventon, this UART protocol is to a Shengyi DWG22
  (custom variant) controller. Keep the `tongsheng_*` function names as literal IDA symbol references only.

## System Timer Tick (TIM2)

The OEM uses TIM2 (not SysTick) as the primary soft-timer tick source for scheduled callbacks:
- TIM2 base: `0x40000000`
- Init: `APP_init_timer2_pwm` @ `0x802129C` enables TIM2 clock (`RCC_APB1ENR` bit 0), configures registers, enables IRQ 28.
- IRQ handler: `IRQ_28_handler` @ `0x8020CF4` checks TIM2 status then calls `sub_8020DD4` which is the callback wheel
  over 24 timer slots.

## Buttons (GPIOC PC0..PC4, input pull-up)

GPIO config:
- `init_gpio_and_scheduler` @ `0x8019720` configures GPIOC pins 0..4:
  - mask `0x001F` (PC0..PC4)
  - mode `0x48` behavior (input pull-up) via `gpio_configure_pins_with_mode(0x40011000, ...)`

Sampling:
- The scheduler callback `sub_80196EC` @ `0x80196EC` reads GPIOC IDR:
  - `sub_80191E2(0x40011000)` reads `*(uint32_t *)(GPIOC + 0x08)` (IDR)
  - masks with `0x1F` (pins 0..4)
  - sets bit `0x20`, then conditionally clears it if none of bits `0x01|0x04|0x10` are set
  - Evidence: disasm @ `0x80196EC` loads `0x40011000` explicitly

Resulting bitfield is consumed as 6 "button slots" by `process_scheduler_state_machine` @ `0x8019488`:
- It debounces `i = 0..5`, mapping each `i` to bit `1<<i` of the sampled byte.

Brake input note (important for OEM alignment):
- In this v2.5.1 app build, we did not find any GPIO reads that look like a dedicated brake switch input.
- Current best hypothesis is that “brake active” is reported by the motor controller over UART (likely `0x52` status,
  payload byte 0 bit 6). See `docs/firmware/OEM_MOTOR_PROTOCOLS_V2.5.1.md`.

Open question:
- The 6th logical button bit (bit 5) is derived, not directly read from a separate GPIO. Its physical meaning
  needs correlation with UI event tables (not mapped in this document yet).

## Battery ADC (ADC1 on PA0)

ADC init:
- `sub_8010C40` @ `0x8010C40`:
  - Enables GPIOA + ADC clocks.
  - Configures PA0 (mask `0x0001`) as analog input (`mode_nibble = 0`).
  - Uses ADC base `0x40012400` (ADC1).

## Unknown / Unmapped Pins (Needs Follow-up)

These are real pin operations seen early in init, but not fully attributed to a named peripheral yet:
- `sub_8018F40` @ `0x8018F40`:
  - Configures PC5 + PC6 (mask `0x0060`) as output open-drain (mode nibble `0x5`) and sets their ODR bits high.
  - Hypothesis: PC5/PC6 are OD-controlled harness outputs or bus lines (lights/lock/etc). Correlate by tracing any
    later writes to GPIOC_BSRR/BRR for bits `0x20`/`0x40` and by checking the physical harness/schematic.
