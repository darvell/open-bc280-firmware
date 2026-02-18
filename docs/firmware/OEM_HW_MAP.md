---
title: OEM Hardware Interaction Map (BC280 3.3.6/4.2.5)
status: draft
source: IDA (BC280_Combined_Firmware_3.3.6_4.2.5.bin)
---

# OEM Hardware Interaction Map

This document captures **evidence-backed** hardware interactions observed in the OEM firmware via IDA.
Addresses and function names are from the active IDA database `BC280_Combined_Firmware_3.3.6_4.2.5.bin`.

## Bootloader BLE OTA (0x55-framed)
- **Dispatcher:** `BOOT_BLE_CommandProcessor` @ `0x8000394`.
- **Frame format:** `0x55` SOF, `cmd`, `len`, payload, checksum (XOR inverted).
- **OTA commands:**
  - `0x22` init: validates flash state, stores expected CRC8 and size, shows “Please BLE Update…”, starts UI task id 11.
  - `0x24` write block: block index + 128 bytes; writes to app image base `+ block_index * 0x80`.
  - `0x26` complete: CRC8 over received size, sets boot flag to exit bootloader, schedules validation task id 6.
- **Helpers:** `BOOT_comm_assemble_frames` @ `0x8000834`, `ble_frame_dispatch` @ `0x8000BAC`,
  `ble_tx_buffer_add_byte` @ `0x8000AB0`, `ble_finalize_and_transmit` @ `0x8000B08`.

## App BLE UART (0x55-framed)
- **Parser:** `ble_uart_frame_parser` @ `0x8011C40` (SOF `0x55`, ring 200, frame ring 7).
- **Dispatcher:** `APP_BLE_UART_CommandProcessor` @ `0x801137C`.
- **Pipeline:** `process_incoming_uart_commands` @ `0x8011A3C` → parser → dispatcher → TX queue.

## Motor Controller UART (Shengyi DWG22, 0x3A-framed)
- **SOF:** `0x3A` (not 0x55).
- **RX:** `tongsheng_uart_rx_ring_push_byte` @ `0x802649C`, `tongsheng_uart_packet_parser` @ `0x80269E4`.
- **Dispatch:** `tongsheng_incoming_packet_processor` @ `0x802598C`.
- **TX:** `motor_uart_txq_enqueue` @ `0x8022BE8`, `motor_uart_irq_handler` @ `0x8027340`.
- **MMIO base used:** `0x40004400` (USART2-style layout).

Note on naming:
- The OEM IDA database and earlier notes used a `tongsheng_*` prefix for this subsystem.
  On Aventon/BC280 hardware this UART link is to a **Shengyi DWG22 (custom variant)** controller; treat `tongsheng_*`
  as a misnomer carried by symbol guesses, not a statement about the actual motor vendor.

## LCD / UI (8080 parallel + DMA)
- **LCD command/data MMIO:** `0x60000000` (cmd), `0x60020000` (data).
- **Init sequence:** `lcd_full_initialization` @ `0x8003AEC` calls
  `lcd_gpio_pins_init` @ `0x80035A4` and `lcd_display_init_sequence` @ `0x8003880`.
- **DMA config path:** `dma_configure_and_enable` @ `0x8003A48` configures a peripheral block using
  `apply_peripheral_configuration` + `toggle_peripheral_enable_bit(0xA0000000, 1)`. No direct DMA base
  address is referenced in code (likely abstracted through peripheral config tables).
- **GPIO bases used for LCD pins:** `0x40010800`, `0x40010C00`, `0x40011400`, `0x40011800`.

## Backlight PWM (TIM1)
- **PWM base:** `0x40012C00`.
- **Init:** `pwm_timer_init` @ `0x80110F8`.
- **Brightness:** `set_screen_brightness_direct` @ `0x80111A8` uses duty steps 0/20/40/60/80/100.
- **Helpers:** `set_motor_pwm_channel` @ `0x8028FDE`, `enable_motor_pwm_output` @ `0x8029252`.

## ADC / Battery Monitoring
- **ADC base:** `0x40012400` (ADC1).
- **Init:** `adc_peripheral_init` @ `0x8010DCC` (GPIOA base `0x40010800` for ADC pin).
- **Sampling:** `process_battery_monitoring` @ `0x8010EA8`.
- **Calibration helpers:** `perform_adc_calibration_sequence` @ `0x8027B74`, `configure_adc_channel` @ `0x8027C46`.

## SPI Flash DMA (DMA1 + SPI1 DR)
- **DMA base:** `0x40020000` (DMA1).
- **SPI1 DR:** `0x4001300C`.
- **Setup:** `spi_flash_dma_setup_channels` @ `0x8019704` uses DMA1 base + SPI1 DR from data table
  (`0x80197F8` = `0x40020000`, `0x80197FC` = `0x4001300C`) and configures CH2/CH3.
- **NVIC:** `sub_801B91A` @ `0x801B91A` sets priorities for IRQs 12/13 (DMA CH2/CH3).

## SPI0 config tables (indirect peripheral config)
- **SPI0 base:** `0x40013000` (loaded via table `0x801FA84`).
- **Workbuf config:** `spi0_apply_workbuf_config` @ `0x8028C68` applies OR‑masked fields from
  a RAM workbuf (`0x20001038` / `0x20001070` / `0x20001054`), and clears CR2 bit `0x800`.
- **Init path:** `spi_flash_dma_init` @ `0x801FA0C` sets SPI0 GPIO pins, applies workbuf config, enables SPI0,
  then configures DMA channels.
  - **Workbuf layout (observed writes):**
    - `workbuf[0] = 0x104` (CR1 base OR mask).
    - `workbuf[1] = 0x0`, `workbuf[2] = 0x0`.
    - `workbuf[3] = 0x200` (bit set in CR1).
    - `workbuf[4] = 0x0`, `workbuf[5] = 0x0`.
    - `workbuf[6] = 0x8` (bit set in CR1).
  - `spi0_apply_workbuf_config` computes: `CR1 = (CR1 & 0x3040) | workbuf[0..6]`, and clears CR2 bit `0x800`.

## Bootloader Mode Flag (SPI flash)
- **Flag location:** `0x003FF080` (SPI flash window).
- **Bootloader check:** `bootloader_main_init` @ `0x8002F14` reads `0x003FF080`, checks `0xAA`.
- **App write:** `write_uint16_to_storage` @ `0x801F710` via `flash_write_data_to_address` @ `0x801F78C`.

## PMU / RTC / I2C / DMA (direct MMIO usage)
- **Observation:** No direct absolute references or xrefs to PMU (`0x400E0000`), RTC (`0x400F0000`),
  I2C1/2/3 (`0x40070000`, `0x400B0000`, `0x400D0000`), or DMA segment base (`0x41100000`) were found
  in the app/boot code segments. This suggests either:
  - These peripherals are unused in this OEM build, or
  - Access is abstracted through ROM/config tables that do not embed absolute addresses.

## DMA config tables (boot + app)
- **Boot DMA UART init:** `dma_uart_init` @ `0x80014BC` uses data tables:
  - `0x8001598` = `0x4002001C` (DMA1 CH2 base), `0x800159C` = `0x4001300C` (SPI1 DR).
  - `0x80015A0`/`0x80015A8` point to RAM config blocks for channel setup.
- **App DMA SPI flash:** `spi_flash_dma_setup_channels` @ `0x8019704` uses tables:
  - `0x80197F8` = `0x40020000` (DMA1 base), `0x80197FC` = `0x4001300C` (SPI1 DR).

## RTC data path (no direct RTC MMIO observed)
- **RTC buffer copy:** `copy_rtc_timestamp_data` @ `0x8019904` copies 7 bytes from RAM buffer
  `0x20000FD4` (no direct RTC register access seen).
- **Power transitions:** `process_power_state_transition_with_rtc` @ `0x801E9D8` uses the copied RTC data
  to update stored stats and schedule tasks.

## Peripheral config schema (apply_peripheral_configuration)
- **Function:** `apply_peripheral_configuration` @ `0x8005D40`.
- **Schema:**
  - `cfg[0]` → peripheral base pointer.
  - `cfg[1..12]` (offsets +4..+0x30) → OR‑mask words combined and written to `base+0x0`.
  - `cfg[13]` (offset +0x34) → pointer to 7‑word table OR‑combined and written to `base+0x4`.
  - If `cfg[11]` (offset +0x2C) == `0x4000`, then `cfg[14]` (offset +0x38) is a table written to `base+0x104`,
    else `base+0x104 = 0x0FFFFFFF`.

## Not Yet Mapped (future work)
- PMU / RTC interaction details.
- DMA channel assignments beyond LCD/flash.
- I2C bus usage for sensors/aux peripherals.
