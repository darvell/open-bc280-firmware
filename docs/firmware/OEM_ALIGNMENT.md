---
title: OEM Alignment Checklist (HW Interaction)
status: draft
source: OEM_HW_MAP.md
---

# OEM Alignment Checklist

This checklist maps OEM hardware behavior to open-firmware implementations.
All “match” entries point to the current code that mirrors OEM behavior.

## Init Ordering (High-Level)
- **OEM:** Main init entry `sub_8026266` performs: PB1 low, clocks, TIM2 timebase, SPI1, LCD+backlight+ADC, BLE pins+UART1,
  BLE protocol registration, PC5/6 pull-ups, config load, motor UART init, buttons init, then schedules power/UI state machine.
- **Open-firmware match:** `docs/firmware/OEM_INIT_ORDER_V2.5.1.md` (mapping + evidence) and `src/main.c` / `platform/board_init.c`.
- **Status:** ✅ mostly aligned; remaining differences are tracked in `docs/firmware/OEM_INIT_ORDER_V2.5.1.md`.

## BLE UART (0x55 framing)
- **OEM:** `0x55` framed BLE UART, app handler `APP_BLE_UART_CommandProcessor` @ `0x801137C`.
- **Open-firmware match:** `src/comm/comm_proto.h` (`COMM_SOF 0x55`) and `src/comm/comm.c`.
- **Status:** ✅ match.

## Motor Controller UART Protocols (0x3A/0x02/0x46 + v2 short frames)
- **OEM:** The v2.5.1 app contains multiple motor-UART parsing/building paths:
  - `0x3A ?? <op> <len> ... <sum16_le> 0x0D 0x0A` (builders commonly use `0x3A 0x1A` as the 2-byte header)
  - `0x02 <len> <cmd> ... <xor8>` (XOR-checksum frames)
  - `0x46` / `0x53` frames with XOR and `0x0D` terminator
  - v2 short fixed frames (5-byte heuristic/checksum rules)
  - Evidence summary: `docs/firmware/OEM_MOTOR_PROTOCOLS_V2.5.1.md`
- **Open-firmware match:**
  - Protocol selection/capture: `src/motor/motor_isr.c`, `src/motor/motor_link.c`
  - `0x3A` builder/validator: `src/motor/shengyi.h`, `src/motor/shengyi.c`
  - `0x02` XOR capture: `src/motor/motor_isr.c`
- **Status:** ✅ wire-format support is aligned enough for capture/probing; field-level semantics are still in-progress per protocol/variant.

## LCD 8080 Bus MMIO
- **OEM:** LCD command/data MMIO `0x60000000` / `0x60020000`.
- **Open-firmware match:** `platform/board_init.c` (`LCD_CMD_ADDR`, `LCD_DATA_ADDR`) and `platform/lcd_dma.c` (`LCD_DATA_ADDR`).
- **Status:** ✅ match.

## LCD Reset (PB0)
- **OEM:** PB0 is configured as a GPIO output and toggled high/low/high during ST7789 init.
- **Open-firmware match:** `platform/board_init.c` (`platform_lcd_bus_pins_init`, `platform_lcd_init_oem_8080`).
- **Status:** ✅ match.

## LCD Init Sequence (ST7789)
- **OEM:** Init sequence via `lcd_display_init_sequence` with ST7789-style commands.
- **Open-firmware match:** `drivers/st7789_8080.c` and `platform/board_init.c` (`platform_lcd_init_oem_8080`).
- **Status:** ✅ match.

## Backlight PWM (TIM1 @ 0x40012C00)
- **OEM:** PWM base `0x40012C00`, brightness steps 0–100 in 20% increments.
- **Open-firmware match:** `platform/hw.h` (`TIM1_BASE 0x40012C00u`) and `platform/board_init.c` (`platform_backlight_init` uses 0/20/40/60/80/100).
- **Status:** ✅ match.

## KEY / Enable Output (PB1)
- **OEM:** PB1 is configured as an output and driven low during early init, then asserted high shortly after the app starts running.
  - Evidence: `sub_801BC2C` drives PB1 low; `tick_timers_init` (`0x801FFE4`) schedules `sub_801F2E4(0)` after 10ms; `sub_801F2E4` case 1 sets PB1 high.
- **Open-firmware match:** `platform/board_init.c` (`platform_key_output_set`) and `src/main.c` (PB1 kept low during init, asserted after ~10ms of TIM2 ticks; cleared on reboot/reset).
- **Status:** ✅ match.

## ADC / Battery Monitoring (ADC1 @ 0x40012400)
- **OEM:** ADC1 base `0x40012400`, OEM-style init sequence + periodic battery monitoring:
  - samples every ~50ms
  - filters with 10-sample window (drop min/max, average remaining)
  - converts using `n69300` scale factor from OEM SPI config block (`0x003FD000/0x003FB000`, offset `0x78`)
- **Open-firmware match:**
  - Init: `platform/board_init.c` (`platform_adc_init`)
  - Runtime: `src/power/battery_monitor.c` (`battery_monitor_tick`) + `src/power/battery_soc.c`
- **Status:** ✅ init + runtime sampling aligned (host sim models EOC/DR for tests).

## Bootloader Mode Flag (SPI flash 0x003FF080)
- **OEM:** Bootloader checks `0x003FF080` byte `0xAA`; app writes via SPI flash helpers.
- **Open-firmware match:** `drivers/spi_flash.c` (`SPI_FLASH_BOOTMODE_ADDR 0x003FF080u`) and `src/comm/handlers.c` / `src/system_control.c` (`spi_flash_set_bootloader_mode_flag`).
- **Status:** ✅ match.

## SPI Flash CS (PA4 manual NSS)
- **OEM:** SPI1 uses a manual chip-select on PA4 (GPIO output), driven high when idle and toggled around transfers.
- **Open-firmware match:** `drivers/spi_flash.c` (`spi_flash_cs_low`, `spi_flash_cs_high`).
- **Status:** ✅ match (manual CS via BSRR/BRR).

## SPI Flash DMA (DMA1 + SPI1 DR)
- **OEM:** DMA1 base `0x40020000`, SPI1 DR `0x4001300C`, CH2/CH3 setup, NVIC 12/13.
- **Open-firmware match:** `drivers/spi_flash.c` (DMA1 CH2/CH3 + SPI1 DR), `platform/irq_dma.c` (IRQ 12/13).
- **Status:** ✅ match.

## Boot DMA UART config tables
- **OEM:** Boot DMA UART init uses DMA1 CH2/CH3 base tables (`0x4002001C`, `0x4001300C`) in `dma_uart_init`.
- **Open-firmware match:** `drivers/spi_flash.c` and `platform/irq_dma.c` mirror DMA1 CH2/CH3 usage.
- **Status:** ✅ match (DMA1 CH2/CH3 + SPI1 DR).

## UART IRQ Priorities / Init
- **OEM:** Motor UART IRQ uses NVIC priority on IRQ 38; BLE UART init and IRQs use USART helpers.
- **Open-firmware match:** `platform/board_init.c` (UART pin + reset, NVIC priority comments) and `platform/uart_irq.c`.
- **Status:** ⚠️ verify IRQ priority mapping vs OEM.

## PMU / RTC / I2C / DMA (direct MMIO)
- **OEM:** No direct absolute references observed to PMU/RTC/I2C/DMA segment bases.
- **Open-firmware match:** N/A (no OEM reference to mirror yet).
- **Status:** ⚠️ unknown — likely abstracted or unused in this OEM build.

## RTC data path (RAM buffer)
- **OEM:** RTC data copied from RAM buffer `0x20000FD4` via `copy_rtc_timestamp_data`.
- **Open-firmware match:** No equivalent RTC persistence path found.
- **Status:** ⚠️ not implemented (OEM uses RAM buffer; no direct RTC MMIO seen).

## SPI0 indirect config tables
- **OEM:** SPI0 config applied via workbuf tables and `spi0_apply_workbuf_config`.
- **Open-firmware match:** `drivers/spi_flash.c` configures SPI1 directly; no workbuf schema.
- **Status:** ✅ behaviorally aligned (SPI1 init + DMA), but schema differs.

## Brake Signal Source (No GPIO Pin Found)
- **OEM:** No dedicated GPIO brake input reads were found in the v2.5.1 app IDA survey (outside of the button sampling on `GPIOC IDR`).
- **Current best hypothesis:** brake state is delivered over the motor UART in the `0x52` status payload:
  - `payload[0]` bit 6 (`0x40`) == brake active (confidence: **M**)
  - Evidence and notes: `docs/firmware/OEM_MOTOR_PROTOCOLS_V2.5.1.md`, `docs/firmware/OEM_PIN_MAP_IDA_V2.5.1.md`
- **Open-firmware status:** ⚠️ not fully aligned yet (we track `g_inputs.brake`, but the `0x52` bit-to-brake mapping is still pending confirmation).

## Gaps / To Validate
- PMU/RTC interactions not fully mapped yet.
- DMA channel mapping for non-LCD paths not fully mapped.
- I2C peripheral usage not mapped.
- PA8 init/config is two-phase in OEM (configured in the LCD pin group init, then reconfigured for TIM1 PWM); ensure
  our GPIO config helpers preserve the OEM “extend/speed” semantics when mirroring those writes (common pitfall:
  treating “extend” as a boolean rather than the speed bits in the 4-bit CRL/CRH nibble).
