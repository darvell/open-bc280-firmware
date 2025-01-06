# open-firmware (BC280-class displays)

Goal: small, auditable application image that the stock bootloader accepts at `0x08010000`, exposing a rich debug surface (peek/poke, flash read, code upload/exec) over UART1 (BLE module passthrough: TTM:CONNECTED + 0x55 frames) and UART2 (motor controller bus). Renode stubs mirror this mapping: UART1 = BLE, UART2 = motor.

## Target MCU
This firmware targets **AT32F403AVCT7** (Cortex‑M4F, 256KB internal flash).

## Preflight before flashing
Run the preflight checker against the image you plan to flash. It validates size + vector table
constraints that the OEM bootloader enforces and ensures the reset PC lands inside the image.

```bash
scripts/preflight_open_firmware.py --image open-firmware/build/open_firmware.bin
```

For an end-to-end emulator sanity run (OEM → bootloader → open-fw → bootflag → power-cycle):

```bash
python3 scripts/renode/test_full_update_flow.py
```
- **Bootloader**: first 64KB (`0x08000000–0x0800FFFF`).
- **App region**: 192KB at `0x08010000–0x0803FFFF`.
- **SRAM**: default 96KB (extendable to 224KB via configuration; we budget for 96KB).

## Build
```bash
cd open-firmware
meson setup build --cross-file cross/arm-none-eabi-clang.txt
ninja -C build
# build/open_firmware.bin
```

## Fast host tests (no Renode)
For quick logic checks (fixed-point helpers, ring buffer min/max, etc.), run the
native host tests:
```bash
meson setup build-host
ninja -C build-host test
```
These tests compile and run on the host toolchain (no ARM toolchain required).

## Host simulator (no Renode)
Run a full fake-bike loop with UART/BLE/sensor shims:
```bash
meson setup build-host
ninja -C build-host host_sim
./build-host/host_sim
```
The sim uses a simple physics model (mass/drag/grade/assist) to compute
speed, cadence, torque, battery sag, and telemetry.
Optional knobs:
- `BC280_SIM_STEPS` (default 60)
- `BC280_SIM_DT_MS` (default 200)
- `BC280_SIM_OUTDIR` (writes `sim_ui_trace.txt`)
- `BC280_SIM_ASSIST` (assist level, default 2)
- `BC280_SIM_MASS_KG` (total mass, default 95.0)
- `BC280_SIM_CRR` (rolling resistance, default 0.010)
- `BC280_SIM_CDA` (drag area, default 0.55)
- `BC280_SIM_GRADE` (grade as ratio, default 0.0)
- `BC280_SIM_WIND_MPS` (headwind, default 0.0)
- `BC280_SIM_WHEEL_R` (wheel radius, default 0.34)
- `BC280_SIM_EFF` (drivetrain efficiency, default 0.85)
When `BC280_SIM_OUTDIR` is set, the sim also emits `shengyi_frames.log`
with the generated 0x52 motor frames and decoded values.

Track work in beads: `open-bc280-firmware-kbu`.
Bus frame mapping from OEM firmware is tracked in `open-bc280-firmware-h1p`.

## Shengyi DWG22 UART frame map (OEM-derived)

The OEM firmware implements a Shengyi DWG22 (custom variant) display↔controller UART protocol with the
following framing (reverse-engineered via IDA from
`BC280_Combined_Firmware_3.3.6_4.2.5.bin`). This map is used by the host sim to
produce realistic controller frames.

**Common framing** (controller↔display):
- **SOF**: `0x3A`
- **Magic**: `0x1A`
- **CMD**: command byte
- **LEN**: payload length in bytes
- **PAYLOAD**: `LEN` bytes
- **CHECKSUM**: 16-bit little-endian sum of bytes 1..(len-4)
- **TAIL**: `0x0D 0x0A`

Checksum logic (from `validate_shengyi_packet_checksum`):
```
uint16_t sum = 0;
for (i = 1; i + 4 < len; ++i)
    sum += buf[i];
checksum = sum & 0xFFFF;
```

### Controller → Display (CMD 0x52, 5-byte motor status)
Parsed in `shengyi_incoming_packet_processor` and `process_shengyi_5byte_motor_data`.
Payload layout:
- **byte 0**: battery voltage code `batt_q` (6-bit, `batt_voltage_mV ≈ 1000 * (batt_q & 0x3F)`)
  - bit 6: motor status flag
  - bit 7: motor status flag
- **byte 1**: current/speed scale `b1` (used as ratio term)
- **byte 2..3**: speed raw (big-endian)
- **byte 4**: motor error code (`0`, `33–38` valid in OEM logic)

Derived fields:
- **battery current (mA)**: `((b1 / 3.0) * 99.9)`
- **speed (kph ×10)**: `(b1 / speed_raw) * wheel_circumference_mm * 3.6`
  - only when `speed_raw < 0x0DAC` (3500)

### Display → Controller (CMD 0x52 request)
Built in `build_shengyi_request_packet_type_0x52`. Payload is 2 bytes:
- **byte 0**: assist level mapped via `sub_801C744(get_assist_level())`
- **byte 1**: flags (bit7=headlight, bit5=walk assist, bit0=speed over-limit)

### Display → Controller (CMD 0x53 config)
Built in `build_shengyi_display_to_controller_packet`, payload length 7:
- **byte 0**: lower 6 bits max assist level, bit6 = lights_off (0 = lights on)
- **byte 1**: gear setting / assist level
- **byte 2**: flags: bit7 motor enable, bit6 brake, bits5..4 speed mode, bits3..0 display setting
- **byte 3–4**: battery voltage threshold + current limit bits (packed; OEM uses non-linear encoding)
- **byte 5**: constant `2`
- **byte 6**: wheel size code (0–7) and speed limit code (bits 3..7)

### Controller → Display (CMD 0xC3 system status)
Built in `send_shengyi_system_status_packet_0xC3`. The payload mirrors the display
config (brightness, auto-off, nominal voltage, profile, light state, assist limits, gear,
brake, speed mode, display setting, battery threshold/current limit/speed limit, wheel
code) followed by units/timeout + wheel circumference and additional status registers.
Payload length is 47 bytes:
- `payload[0..2]`: brightness, auto-off, nominal voltage
- `payload[3..10]`: profile, light state, max assist, gear, motor enable, brake, speed mode, display setting
- `payload[11..12]`: battery threshold mV (big-endian)
- `payload[13]`: current limit (A) = mA / 1000
- `payload[14]`: speed limit (kph) = kph_x10 / 10
- `payload[15]`: wheel size code (0–7)
- `payload[16..20]`: param_0281, motor_status_timeout_s, param_027E, units_mode, flag_026F
- `payload[21..22]`: wheel circumference mm (big-endian)
- `payload[23..31]`: param_0234, param_0270, param_0271, param_0267, param_0272, param_0273, param_0274, param_0275, param_0262
- `payload[32..35]`: motor current and power reported (big-endian words)
- `payload[36]`: constant `1`
- `payload[37]`: param_0235
- `payload[38..39]`: param_021C (big-endian)
- `payload[40..41]`: param_0238 (big-endian)
- `payload[42..43]`: param_0230 (big-endian)
- `payload[44..46]`: param_023A, param_023B, param_023C

### Display → Controller (CMD 0xC2 status request)
`shengyi_incoming_packet_processor` treats CMD 0xC2 as a request to send the
system status packet (0xC3). The display sends an empty payload frame and the
controller responds with 0xC3.

### Display → Controller (CMD 0xC0 config update)
Parsed in `shengyi_incoming_packet_processor` when `CMD==0xC0`. Payload layout:
- `payload[0]` screen_brightness_level (0–5)
- `payload[1]` auto_poweroff_minutes (0–10)
- `payload[2..6]` datetime (year offset from 2000, month, day, hour, minute)
- `payload[7]` batt_nominal_voltage_V (24/36/48)
- `payload[8]` config_profile_id
- `payload[9]` lights_enabled
- `payload[10]` max_assist_level (2–64)
- `payload[11]` gear_setting
- `payload[12]` motor_enable_flag
- `payload[13]` brake_flag
- `payload[14]` speed_mode (0–3)
- `payload[15]` display_setting (0–15)
- `payload[16..17]` batt_voltage_threshold_mV (big-endian)
- `payload[18]` batt_current_limit_mA / 1000
- `payload[19]` speed_limit_kph_x10 / 10 (10–51)
- `payload[20]` wheel size code (0–7; mapped as in C3)
- `payload[21]` param_0281 (1–60)
- `payload[22]` motor_status_timeout_s (>=5 → timeout_ms = *1000)
- `payload[23]` param_027E (0–10)
- `payload[24]` units_mode (0/1)
- `payload[25]` flag_026F (0/1)
- `payload[26..27]` wheel_circumference_mm (big-endian)
- `payload[28..36]` param_0234, param_0270, param_0271, param_0267, param_0272, param_0273, param_0274, param_0275, param_0262
- `payload[37..38]` motor_current_mA_reported (big-endian)
- `payload[39..40]` motor_power_W_reported (big-endian)
- `payload[41]` motor_temp_C
- `payload[42]` param_0235
- `payload[43..44]` param_021C (big-endian)
- `payload[45..46]` param_0238 (big-endian)
- `payload[47..48]` param_0230 (big-endian)
- `payload[49..51]` param_023A, param_023B, param_023C

### Other OEM commands in `shengyi_incoming_packet_processor`
- `0xA6`: read 64 bytes from flash config slot (returns 65-byte payload)
- `0xA7`: write 4 bytes to flash config slot (optionally triggers BLE init)
- `0xA8`: write variable-length config slot (echoes slot + status)
- `0xA9`: read config slot (variable-length response based on slot id)
- `0xAA`: display mode/assist update (calls `handle_display_mode_and_assist_level`)
- `0xAB`: select protocol mode (byte0 enable, byte1 mode 0–3)
- `0xAC`: battery voltage calibration/query (byte0 selects calibrate vs report)
- `0xAD`: protocol init + event handler registration
- `0xB0`: return status flags block (12-byte payload)

Command details (OEM):
- `0xA6` request: empty payload; response payload length 65, first byte `0x40` then 64 bytes from flash.
- `0xA7` request: payload `[slot, b0, b1, b2, b3, reinit_ble]` writes 4 bytes to config slot.
- `0xA8` request: payload `[slot, len, data...]` variable length write; response `[slot, success]`.
- `0xA9` request: payload `[slot]`; response length depends on slot contents:
  - slots 0–4,8: first byte in flash is length (1–0x40), followed by that many bytes
  - slots 5–6: 4 bytes (buffer[1..4])
  - slot 7: 32 bytes (buffer[1..32])
  - otherwise: length 0
- `0xAA` request: payload `[display_mode_assist_raw]`.
- `0xAB` request: payload `[enable, mode]` sets protocol mode and re-initializes.
- `0xAC` request: payload `[calibrate]` responds with either battery calibration status or current batt voltage (u32 big-endian).
- `0xB0` response: 12 bytes pulled from system status flags.

### OEM status packet (non-0x3A) built in `shengyi_build_status_packet`
This is a separate Shengyi status frame assembled via `shengyi_packet_add_byte`
and finalized with an XOR checksum. The sender transmits the data bytes plus checksum
only (buffer offset +2, length includes checksum). Byte layout (19 data bytes + 1 checksum):
- `byte 0` frame type (always `0x01` in OEM)
- `byte 1` length (always `0x14`, equals data+checksum length)
- `byte 2` frame counter (always `0x01` in OEM)
- `byte 3` profile type (pad_0250[0], 3/5/9)
- `byte 4` power level (mapped assist → 0–15)
- `byte 5` status flags (bit7 always 1; bit6 brake; bit5 auth; bit3 headlight; bit2 low battery; bit1 error; bit0 startup; bit4 reserved)
- `byte 6` display setting
- `byte 7..8` wheel_size_x10 (big-endian)
- `byte 9` battery current raw (pad_0250[4])
- `byte 10` battery voltage raw (pad_0250[5])
- `byte 11` controller temperature raw (pad_0250[7])
- `byte 12` speed limit (kph)
- `byte 13` current limit (A)
- `byte 14..15` battery threshold (mV/100, big-endian)
- `byte 16..17` reserved (0)
- `byte 18` status2 (low nibble error code)
- `byte 19` XOR checksum of bytes 0..18

## Host MCU peripheral shim (C scope)
For host-only unit tests (no Renode), we provide a minimal MCU peripheral shim
that mirrors the **subset** of STM32F1 MMIO interactions observed in OEM/open
firmware flows:
- RCC reset flags (CSR, RMVF clear)
- IWDG (KR/PR/RLR + reset flag)
- GPIO IDR/ODR/BSRR/BRR (input sampling + output set/reset)
- UART1/2/4 (SR/DR/BRR/CR1, RXNE/TXE, basic RX/TX queues)
- ADC1 DR (single-channel sample on SWSTART)
- SPI flash backing store for bootloader flag + config/data

The shim lives under `open-firmware/tests/host/` and is **not** compiled into
embedded builds. It exists purely to keep the host sim behavior aligned with OEM
peripheral expectations when Renode is not available.

## Host LCD dump (no Renode)
The host sim can emit a crude LCD dump as a PPM image so you can see the UI
without Renode:

```bash
meson setup build-host
ninja -C build-host host_sim
./build-host/host_sim
open open-firmware/tests/host/lcd_out/host_lcd_latest.ppm
```

Set `UI_LCD_OUTDIR` (or `BC280_LCD_OUTDIR`) to change the output directory.

### UI screenshots (PNG)
Generate PNG screenshots for all UI pages via the batch script:
```bash
./scripts/gen_screenshots.sh
```
Outputs go to `docs/examples/` (tracked in git) with 2× scaled PNGs for each page.
The script builds `host_sim`, forces each page in sequence, and converts PPM → PNG.

**UI Gallery** — see [docs/examples/](docs/examples/) for current screenshots:
- `dashboard_screen.png` — main riding view (speed hero, stats tray, top bar)
- `trip_screen.png` — trip summary (distance, time, avg speed)
- `settings_screen.png` — settings menu
- `battery_screen.png` — battery detail (voltage, current, SoC)
- `thermal_screen.png` — thermal monitoring
- `graphs_screen.png` — strip chart graphs
- (and more: profiles, cruise, diagnostics, bus, capture, tune, etc.)

### UI assets
The UI uses a compact 1-bit packed bitmap font (~814 bytes for ASCII 32-126) and vector-style primitives (no sprite packs or full framebuffer).

### Host sim button inputs
By default the host sim pulses the page buttons at steps 12/22 to flip pages.
Override with:
- `BC280_SIM_BUTTONS=0x10` (fixed OEM button mask each tick)
- `BC280_SIM_BUTTONS_SEQ="12:0x10,22:0x02"` (per-step OEM mask)
Optional:
- `BC280_SIM_BUTTON_MAP=0|1|2` (apply button map presets)
- `BC280_SIM_QA_FLAGS=0x1C` (enable quick actions: cruise/profile/capture)

Short press fires on release; long press fires after ~800 ms hold.

Buttons are sampled via the host GPIO shim using the OEM path: GPIOC IDR bits
0–4 (active-low), with bit5 forced high. The sim sets GPIOC inputs from
`BC280_SIM_BUTTONS(_SEQ)` and then reads back the IDR to produce the same
`buttons_sample_GPIOC_IDR` behavior seen in OEM firmware.

OEM button bit mapping (GPIOC, active-low):
- bit0: Up
- bit1: Power
- bit2: Down
- bit3: Light
- bit4: Menu

Host sim translates OEM buttons into internal masks:
- Up → `BUTTON_GEAR_UP_MASK` (0x10)
- Down → `BUTTON_GEAR_DOWN_MASK` (0x20)
- Up+Down → `WALK_BUTTON_MASK` (0x40)
- Power → `UI_PAGE_BUTTON_POWER` (0x08)
- Menu → `UI_PAGE_BUTTON_RAW` (0x04)
- Light → `CRUISE_BUTTON_MASK` (0x80)

### Display → Controller (CMD 0xC0 config update)
Parsed in `shengyi_incoming_packet_processor` when `CMD==0xC0` with extended payload
(> 50 bytes). Fields include speed limit, wheel circumference, nominal voltage, assist
profile, motor enable, brake flag, battery thresholds, motor current/power/temperature
readbacks, and additional config registers.


- `packet[42]` param_0262
- `packet[43..44]` motor_current_mA_reported (little-endian)
- `packet[45..46]` motor_power_W_reported (little-endian)
- `packet[47]` motor_temp_C
- `packet[48]` param_0235
- `packet[49..50]` param_021C (big-endian)
- `packet[51..52]` param_0238 (big-endian)
- `packet[53..54]` param_0230 (big-endian)
- `packet[55]` param_023A
- `packet[56]` param_023B
- `packet[57]` param_023C

Wheel size code mapping (from switch):
- code 0 → wheel_size_x10=160, circ=1276
- code 1 → 180, 1436
- code 2 → 200, 1595
- code 3 → 220, 1755
- code 4 → 240, 1914
- code 5 → 260, 2074
- code 6 → 275, 2193
- code 7 → 290, 2313

## Load in Renode
Use `renode/bc280_open_firmware.resc`:
```bash
renode/Renode.app/Contents/MacOS/renode --disable-xwt \
  -e "include @renode/bc280_open_firmware.resc"
```
The script:
- Loads the vendor combined firmware for the bootloader at `0x08000000`.
- Overlays `build/open_firmware.bin` at `0x08010000` (and mirror 0x00010000).
- Starts execution from the bootloader Reset vector so the normal boot path validates and jumps to the new app.

UART1 output is streamed via the Renode USART1 stub; you’ll see status lines once per second plus responses to commands. UART1 maps to the BLE module passthrough on real hardware. UART2 maps to the motor controller bus.

## Debug protocol

### Host helper for Renode/PTY
For headless Renode tests we expose USART1 over a host PTY. Set
`BC280_UART1_PTY=/tmp/uart1` before launching Renode; the `usart1_stub` will
create a symlink at that path pointing to the live PTY. The helper at
`scripts/renode/uart_client.py` speaks the 0x55-framed protocol without extra
dependencies:

```bash
BC280_UART1_PTY=/tmp/uart1 \
  renode/Renode.app/Contents/MacOS/renode ...

scripts/renode/uart_client.py --port /tmp/uart1 ping
scripts/renode/uart_client.py --port /tmp/uart1 set-state --rpm 220 --torque 50 --speed-dmph 123 --soc 87 --err 1
scripts/renode/uart_client.py --port /tmp/uart1 stream --period-ms 500 --duration-ms 1500
scripts/renode/uart_client.py --port /tmp/uart1 reboot-bootloader
```

The companion test script `scripts/renode/test_uart_protocol.py` runs a smoke
sequence (ping, state roundtrip, streaming cadence) against `/tmp/uart1` and is
suitable for CI once Renode is available in the environment. Additional
scenario tests live under `scripts/renode/`, e.g. `test_walk_assist.py` and
`test_brake_override.py` for safety cutouts.

Frame format (both UART1 + UART2):
```
 0x55 | CMD | LEN | PAYLOAD... | CHKSUM
```
`CHKSUM` = bitwise-not of XOR over all preceding bytes in the frame.

Responses use `CMD|0x80` unless noted.

Status codes (when a 1-byte status is returned):
- `0x00` OK
- `0xFC` blocked by safety gating (e.g., config change while moving)
- `0xFE` invalid payload or range
- `0xFF` unsupported/unknown

Supported commands:
- `0x01` ping → status(0)
- `0x02` read32: payload addr[4] → returns 4 bytes
- `0x03` write32: addr[4], value[4] → status
- `0x04` read_mem: addr[4], len[1] → len bytes
- `0x05` write_mem: addr[4], len[1], data → status
- `0x06` exec: addr[4] (Thumb) → jumps after ACK
- `0x07` upload+exec: addr[4], len[1], data → write then jump
- `0x08` read_flash: flash_addr[4], len[1] → len bytes
- `0x0A` state dump → 16-byte struct {ms, rpm, torque, speed_dmph, soc, err, last_ms_lo16}
- `0x0B` set bootloader flag (writes 0xAA to `g_bootloader_mode_flag` in SPI flash) → status
- `0x0C` set state: rpm[2], torque[2], speed_dmph[2], soc[1], err[1] → status
- `0x0D` set streaming period: period_ms[2]; 0 disables. When enabled, device emits cmd `0x81` telemetry v1 frames (22-byte, versioned payload).
- `0x0E` reboot to bootloader: sets flag then jumps via bootloader vectors.
- `0x20` ring buffer summary (speed samples): returns {count[2], capacity[2], min[2], max[2], latest[2]} for the internal speed ring buffer (64-slot, power-of-two, O(1) min/max).
- `0x21` debug state v19 → 122-byte, versioned struct for tools. Fields (big endian):
  - ver=19, len=122 (v2 added cap_*; v3 adds curve_* derived values; v4 adds gear/cadence bias internals; v5 adds walk state; v6 adds mode + effective caps; v8 adds governor limits + duty/thermal/sag data; v9 adds soft-start ramp state; v10 adds reset flags + raw CSR; v11 adds range estimate fields; v13 adds drive mode + boost fields; v15 adds regen + hw caps; v17 adds lock + quick-action state; v19 adds adaptive assist fields).
  - ms[4]
  - inputs: last_ms[4], speed_dmph[2], cadence_rpm[2], torque_raw[2], throttle_pct[1], brake[1], buttons[1]
  - outputs: assist_mode[1], profile_id[1], virtual_gear[1], cmd_power_w[2], cmd_current_dA[2], cruise_state[1] (0=off,1=speed,2=power), adapt_clamp_active[1]
  - caps: cap_power_w[2], cap_current_dA[2], cap_speed_dmph[2] (effective caps after config/legal clamps)
  - curve-derived: curve_power_w[2] (speed→power after cadence multiplier), curve_cadence_q15[2], cap_speed_mirror[2]
  - gear/cadence: gear_limit_power_w[2], gear_scale_q15[2], cadence_bias_q15[2]
  - walk: walk_state[1], walk_cmd_power_w[2], walk_cmd_current_dA[2]
  - mode/caps: mode[1] (0=street, 1=private), cap_effective_current_dA[2], cap_effective_speed_dmph[2], adapt_speed_delta_dmph[2]
  - governors: P_user[2], P_lug[2], P_therm[2], P_sag[2], P_final[2], limit_reason[1], adapt_trend_active[1]
  - governor internals: duty_q16[2], I_phase_est_dA[2], thermal_state[2], sag_margin_dV[2]
  - soft-start: ramp_active[1], reserved[1], ramp_out_w[2], ramp_target_w[2]
  - reset: reset_flags[2], reset_csr[4] (raw RCC_CSR). reset_flags bit mapping: 0=BOR,1=PIN,2=POR,3=SOFT,4=IWDG,5=WWDG,6=LPWR.
  - range: range_wh_per_mile_d10[2], range_est_d10[2] (0.1 mi/km based on units), range_confidence[1] (0–100), range_samples[1]
- drive: drive_mode[1], drive_setpoint[2], drive_cmd_power_w[2], drive_cmd_current_dA[2]
- boost: boost_budget_ms[2], boost_active[1], boost_threshold_dA[2], boost_gain_q15[2]
- regen: hw_caps[1], regen_supported[1], regen_level[1], regen_brake_level[1], regen_cmd_power_w[2], regen_cmd_current_dA[2]
- lock/quick-action: lock_enabled[1], lock_active[1], lock_allowed_mask[1], quick_action_last[1]
- `0x22` graph summary (active channel/window): returns {count[2], capacity[2], min[2], max[2], latest[2], period_ms[2], window_ms[2]} for the selected strip chart ring.
- `0x23` graph control: payload {channel[1], window[1], flags[1?]}. Selects the active strip chart. `flags` bit0 resets the selected channel buffers. Channels: 0=SPD, 1=W, 2=V, 3=CAD, 4=TEMP. Windows: 0=30s, 1=2m, 2=10m.
- Config writes are allowed only when speed ≤ 1.0 mph (10 dMPH); otherwise status `0xFC`.
- `0x30` config_get: returns the active config blob (81 bytes: ver,size,reserved,seq,crc32,wheel_mm,units,profile_id,theme,flags,button_map,button_flags,mode,pin_code,cap_current_dA,cap_speed_dmph,log_period_ms,soft_start_ramp_wps,soft_start_deadband_w,soft_start_kick_w,drive_mode,manual_current_dA,manual_power_w,boost_budget_ms,boost_cooldown_ms,boost_threshold_dA,boost_gain_q15,curve_count,curve[8] {x,y}).
- `0x31` config_stage: payload is a 81-byte config blob (CRC checked). Firmware bumps seq and recalculates CRC, keeps it staged.
- `0x32` config_commit: payload reboot_flag[1]; writes staged blob atomically to the alternate slot, makes it active, and optionally reboots to the app.
- `0x33` set_profile: payload {id[1], persist[1]=1}; applies an assist profile (0–4), persists to config slots if requested, and updates debug outputs immediately.
- `0x34` set_gears: payload {count[1], shape[1], min_q15[2], max_q15[2], optional scales...}; defines up to 12 virtual gears (linear or exponential step shape). Buttons bit4=up, bit5=down advance the active gear.
- `0x35` set_cadence_bias: payload {enabled[1], target_rpm[2], band_rpm[2], min_bias_q15[2]}; above target cadence, assist is tapered toward `min_bias_q15`.
- `0x38` set_drive_mode: payload {mode[1], setpoint[2]}. mode: 0=auto, 1=manual current (deci-amps), 2=manual power (W), 3=sport (boost budget).
- `0x39` set_regen: payload {level[1], brake_level[1]} sets regen strength (0–10) and brake-blend strength. Returns 0xFD if regen capability is unsupported.
- `0x3A` set_hw_caps: payload {caps[1]} overrides runtime hardware capability flags (bit0=walk, bit1=regen) for testing.
- `0x36` trip_get: returns active and last trip snapshots (versioned); payload {ver,size,flags,active(24B),last(24B)}.
- `0x37` trip_reset: finalizes current trip into last summary (persisted) and clears active accumulators.
- `0x40` event_log_summary: returns {ver,size,count[2],capacity[2],head[2],record_size[2],reserved[2],seq[4]}.
- `0x41` event_log_read: payload {offset[2], limit[1<=8]} → {count[1], records...}; records are 20-byte BE snapshots {ms[4],type[1],flags[1],speed_dmph[2],batt_dV[2],batt_dA[2],temp_dC[2],cmd_power_w[2],cmd_current_dA[2],crc16[2]} ordered oldest→newest.
- `0x42` event_log_mark: payload {type[1],flags[1]} appends a record using current inputs/outputs snapshot (reserved for diagnostics/tests).
- `0x44` stream_log_summary: returns {ver,size,count[2],capacity[2],head[2],record_size[2],period_ms[2],enabled[1],reserved[1],seq[4]}.
- `0x45` stream_log_read: payload {offset[2], limit[1<=8]} → {count[1], records...}; records are 20-byte BE samples {ver[1],flags[1],dt_ms[2],speed_dmph[2],cadence_rpm[2],power_w[2],batt_dV[2],batt_dA[2],temp_dC[2],assist_mode[1],profile_id[1],crc16[2]} ordered oldest→newest. flags bit0=brake, bit1=walk.
- `0x46` stream_log_control: payload {enable[1], optional period_ms[2]} enables/disables stream logging; period defaults to config log_period_ms when omitted.
- `0x47` crash_dump_read: returns a fixed-size crash dump snapshot (152 bytes). Layout (big-endian): magic 'CRSH', version, size, flags, seq, crc32, ms, sp, lr, pc, psr, cfsr, hfsr, dfsr, mmfar, bfar, afsr, event_count, event_record_size, event_seq, event_records[4] (raw 20-byte event log records). If no dump is present, payload is zeroed.
- `0x48` crash_dump_clear: clears crash dump storage (status).
- `0x50` bus_capture_summary: returns {ver,size,count[2],capacity[2],head[2],max_len[1],enabled[1],seq[4]}.
- `0x51` bus_capture_read: payload {offset[2], limit[1<=8]} → {count[1], records...}; records are {dt_ms[2],bus_id[1],len[1],data[len]} ordered oldest→newest.
- `0x52` bus_capture_control: payload {enable[1], reset[1?]} enables/disables capture; reset clears the ring.
- `0x53` bus_capture_inject: payload {bus_id[1], dt_ms[2], len[1], data[len]} → status. Requires private mode + armed injection + capture enabled; default gating also requires stationary + brake unless override is set. Successful injects append to the capture ring and event log.
- `0x54` bus_monitor_control: payload {flags[1], bus_id[1?], opcode[1?]} → status. Flags: bit0 enable, bit1 filter_id, bit2 filter_opcode, bit3 diff mode, bit4 changed-only view, bit5 reset view/prev. When filter flags are set, bus_id/opcode are matched against frames (opcode = first data byte).
- `0x55` bus_inject_arm: payload {armed[1], override[1?]} → status. override bypasses speed/brake gating (still requires private mode + armed).
- `0x56` bus_capture_replay: payload {mode[1], offset[1], rate_ms[2]} → status. mode=0 stops replay. mode=1 replays captured frames starting at offset, bounded rate (20–1000 ms). Brake edge cancels replay unless override is enabled.
- `0x70` ble_hacker_exchange: payload is a custom GATT control-plane frame `{ver, op, len, payload...}`. Response payload is the encoded response frame (`op|0x80`) with a leading status byte in the response payload (0=OK, 0xF4 blocked by safety gating, 0xFD/0xFE for config errors, 0xF0+ for framing).
- `0x71` ab_status: returns {ver,size,active_slot,pending_slot,last_good_slot,flags,build_id}. flags bit0=active_valid, bit1=pending_valid.
- `0x72` ab_set_pending: payload {slot}. slot=0/1 to mark pending A/B slot, or 0xFF to clear pending; applied on next boot via OEM bootloader path.
- `0x7D` log_frame: no payload; responds with the stored last-log record. Payload format: `{code[1], len[1], data[len]}` (max total 64). Response uses cmd `0x7D` (not ORed). `code` is user-defined; `data` is binary params.

All other commands → status(0xFF).

Telemetry stream (cmd 0x81, versioned v1 payload):
```
byte 0 : version (=1)
byte 1 : size (=22)
2..5   : ms (BE)
6..7   : speed_dmph
8..9   : cadence_rpm
10..11 : power_w
12..13 : battery_dV (0.1 V, signed)
14..15 : battery_dA (0.1 A, signed)
16..17 : ctrl_temp_dC (0.1 C, signed)
18     : assist_mode
19     : profile_id
20     : virtual_gear
21     : flags bit0=brake, bit1=walk_active
```

Versioning / compatibility:
- Existing tools relying on `0x0A` (16-byte state) remain unchanged.
- New structured state lives at `0x21` and carries an explicit `version` and `len` at bytes 0–1. Add fields by bumping the version number and documenting the layout; older tools can gate on `version` to stay compatible.

### Telemetry spec + parser
- Protocol spec (versioned): `open-firmware/docs/telemetry_protocol.md`
- Parse Renode UART logs (extracts 0x81 telemetry frames from `uart1_tx.log`):
```bash
BC280_RENODE_OUTDIR=/tmp/bc280_renode_out \
  scripts/renode/parse_uart_log.py --telemetry-only --json
```

## Quick host helper (python)
```python
import serial, struct
# PTY ignores baud; real hardware UART1 (BLE) is 9600 on OEM setups.
s = serial.Serial('/tmp/uart1', 9600, timeout=0.2)

def send(cmd, payload=b''):
    frm = bytearray([0x55, cmd, len(payload)])
    frm += payload
    cks = (~(sum(frm[i] ^ 0 for i in range(len(frm))) & 0xFF)) & 0xFF
    frm.append(cks)
    s.write(frm)
    return s.read(260)

# ping
print(send(0x01))

# read32 SCB->VTOR
print(send(0x02, struct.pack('>I', 0xE000ED08)))

# set state (rpm=220, tq=50, speed=12.3 mph=123 deci-mph, soc=87, err=1)
print(send(0x0C, struct.pack('>HHHBB', 220, 50, 123, 87, 1)))

# enable 500 ms streaming
print(send(0x0D, struct.pack('>H', 500)))

# reboot to bootloader
print(send(0x0E))
```

## Portability knobs
- `link.ld`: keeps `_stack_top < 0x20020000` to satisfy the stock bootloader’s SP mask check.
- `main.c`: UART bases pulled from `renode/bc280_platform.repl`; adjust for other boards by changing `UART*_BASE` constants.
- SPI flash offset mirrors what IDA showed (`g_bootloader_mode_flag` at `0x003FF080`, accessed via SPI1 with CS on PA4).

## Status overlay
Once per second the firmware prints:
```
[open-fw] t=<ms> ms rpm=<rpm> tq=<torque> speed=<x.y> soc=<pct> err=<code>
```
Motor/state fields are writable via the command protocol; hook your own sensor/parsers by populating `g_motor`.

## UI plan (future)
Goal: a small, “beautiful” UI that fits without bitmap assets or a full framebuffer.
See `UI_SPEC.md` for a vector-ish rendering plan (dirty rects, stroked digits, procedural icons) and a Renode-testable trace strategy.

## Setup wizard (MVP)
The on-device setup wizard uses button combos (no LCD UI yet):
- Enter: press Walk + Cruise together.
- Navigate: Cruise = next, Walk = back, Gear Up/Down = adjust.
- Steps: wheel circumference → units → button map → profile → commit.

## Power policy plan (future)
Goal: fixed-function (no scripting) “multi-governor” control policy that protects motors (lugging/thermal), avoids brownouts (sag), and stays testable in Renode.
See `POWER_CONTROL_SPEC.md`.
