---
title: OEM Motor UART Protocols (BC280 App v2.5.1)
status: draft
oem_binary: B_JH_FW_APP_DT_BC280_V2.5.1.bin
---

# OEM Motor UART Protocols (BC280 App v2.5.1)

This documents the distinct **wire formats** observed in the OEM v2.5.1 app
for traffic on the motor UART (USART2 @ `0x40004400`).

Important: OEM/IDA symbol prefixes (notably `tongsheng_*`) are **not trusted**
as ground truth. Everything below is derived from packet parsing / checksum
code and literal byte constants.

## Summary (Signatures)

Observed motor-UART wire formats in OEM v2.5.1:
- `0x3A ?? <op> <len> ... <sum16_le> 0x0D 0x0A` (most common; builders typically use `0x3A 0x1A` as the 2-byte header)
- `0x02 <len> <cmd> ... <xor8>` (STX-like; XOR over all bytes except the last)
- `0x46 ... <xor8_excluding_byte0> 0x0D` (auth-ish; also seen with `0x53`)
- “v2” short fixed frames (5 bytes), checksum-like byte relationships (heuristic)

## Protocol A: 0x3A/0x1A Header, LEN, SUM16, CRLF (Open-Firmware: `MOTOR_PROTO_SHENGYI_3A1A`)

Evidence (OEM v2.5.1):
- Frame builder finalizer `ui_cmd_finalize_enqueue` @ `0x8025600`:
  - Writes `frame[3] = (len_before_finalize - 4)` as payload length.
  - Computes 16-bit sum of bytes `frame[1..len-4]` and appends LSB then MSB.
  - Appends CR (`0x0D`) and LF (`0x0A`).
- Parser `tongsheng_uart_packet_parser` @ `0x8025068`:
  - Looks for SOF byte `0x3A` (58).
  - Does not appear to enforce a specific “second header” byte; OEM builders typically use `0x1A` at byte 1.
  - Uses the length byte at offset 3 (once accounting for slot layout).
  - Validates checksum via `validate_tongsheng_packet_checksum` @ `0x8024084`.
- Periodic send builds explicit headers:
  - `sub_80254C8` prepends `0x3A 0x1A 0x52 0x07 ...` then finalizes.
  - `sub_8025260` prepends `0x3A 0x1A 0x53 0x07 ...` then finalizes.

Wire format:
- Byte 0: `0x3A` (SOF)
- Byte 1: “second header” (commonly `0x1A` in v2.5.1 builders; OEM RX likely accepts other values)
- Byte 2: opcode / cmd
- Byte 3: payload_len
- Byte 4..: payload
- Trailing: `sum16_le` (LSB, MSB), then `0x0D 0x0A`

Notes:
- This is the format we previously documented as “Shengyi DWG22”.
- OEM IDA calls this “tongsheng” in multiple symbols, but that naming is not used
  as truth in open-firmware anymore.

### Protocol A: Brake Signal (Likely)

Hypothesis (OEM v2.5.1 and on-bike correlation required):
- Brake state is likely carried in the `0x52` status payload:
  - `payload[0]` bit 6 (`0x40`) == brake active
  - Confidence: **M** (medium)

Rationale:
- `0x52` status payload byte 0 is already a bitfield in open-firmware decoding:
  - lower 6 bits (`0x3F`) behave like battery voltage-in-volts (dV = (b0 & 0x3F) * 10)
  - bit 7 (`0x80`) is used as an error indicator in current open-firmware decoding
- That leaves bit 6 (`0x40`) as a plausible OEM status flag (brake, cut, inhibit, etc).

Important pin-map note:
- In the v2.5.1 OEM app IDA survey we did **not** find any dedicated GPIO brake input reads; the only recurring
  GPIO input sampling is the button matrix on `GPIOC IDR` pins 0..4.
- This reinforces the likelihood that “brake” is a motor-protocol-derived status rather than a direct MCU GPIO.
  See `docs/firmware/OEM_PIN_MAP_IDA_V2.5.1.md`.

## Protocol B: 0x02 SOF, LEN, XOR (Open-Firmware: `MOTOR_PROTO_STX02_XOR`)

Evidence (OEM v2.5.1, mode 1 path):
- Parser `sub_8022136` @ `0x8022136`:
  - Waits for SOF byte `0x02`.
  - Uses the byte after SOF as an expected length.
  - When collected bytes >= expected length, validates XOR via `sub_8021610` @ `0x8021610`.
- XOR check `sub_8021610`:
  - XOR of all bytes except the last equals the last byte.
- Dispatch `sub_802164C`:
  - Interprets `cmd` at byte index 2 (`n168 = frame[2]`).
  - For `cmd == 1`, passes a 10-byte payload to `APP_process_motor_response_packet`.

Wire format (inferred from parser and handler byte offsets):
- Motor → display frames start with `0x02` (SOF).
- Display → motor frames start with `0x01` (SOF) in the OEM v2.5.1 builders
  (built in `APP_process_motor_control_flags` @ `0x80222D4`; see `docs/firmware/README.md`).
- Byte 0: SOF (`0x02` for motor→display, `0x01` for display→motor)
- Byte 1: `len_total` (includes checksum byte)
- Byte 2: `cmd`
- Byte 3..(len_total-2): payload
- Byte (len_total-1): `xor8(frame[0..len_total-2])`

### Protocol B: `cmd==1` Status Payload (10 bytes)

Evidence anchors (OEM v2.5.1):
- `cmd==1` dispatch in `sub_802164C` → `APP_process_motor_response_packet` @ `0x8021CA8`
- Speed cutoff compares `period_ms` against `0x0BB8` (3000ms) inside `0x8021CA8`
- Current scaling logic uses bit `0x4000` of a 16-bit big-endian field at payload bytes 2..3

Frame layout:
- `[0]` `0x02` (SOF)
- `[1]` total length (`0x0E` for cmd1)
- `[2]` `cmd` (`0x01`)
- `[3..12]` payload (10 bytes)
- `[13]` XOR checksum

Payload layout (`p = &frame[3]`), with confidence based on direct disassembly dataflow:

| Payload Byte(s) | Meaning | Notes |
|---|---|---|
| `p[0]` | `flags` | H: Used for error mapping + stores bit2/bit7 booleans. |
| `p[1]` | unknown/reserved | L: Not referenced in the `cmd==1` handler. |
| `p[2..3]` | `motor_current` (scaled) | H: Big-endian 16-bit. Magnitude is `raw & 0x3FFF`. If `raw & 0x4000`: units are 0.1A (mA = mag*100). Else: units are 1A (mA = mag*1000). Stored as u16 in app state at offset `+0x16A` and later used in power math. |
| `p[4]` | unknown/reserved | L: Not referenced in the `cmd==1` handler. |
| `p[5..6]` | `period_ms` | H: Big-endian ms per wheel revolution. If `period_ms >= 3000`, OEM forces speed=0. Else speed computed from wheel circumference and `3.6` conversion. |
| `p[7]` | `soc_pct` (optional) | M: If a config/state selector (`+0x171`) equals 1, OEM copies this byte into `+0x170`. Looks like a percent. Otherwise ignored. |
| `p[8]` | unknown/reserved | L: Not referenced in the `cmd==1` handler. |
| `p[9]` | unknown/reserved | L: Not referenced in the `cmd==1` handler. |

`flags` bit usage (H for observed mapping; semantics beyond mapping may still be unknown):
- bit1 => error code `2`
- bit3 => error code `6`
- bit0 => error code `7`
- bit5 => error code `8`
- bit4 => error code `9`
- bit6 => error code `20`
- else => error code `0`
- bit2 => stored to state byte `+0x181` (semantics TBD)
- bit7 => stored to state byte `+0x183` (semantics TBD)

### Protocol B: Display → Motor “0x14 Status/Control” Packet (20 bytes incl XOR)

Evidence anchors (OEM v2.5.1):
- Builder: `APP_process_motor_control_flags` @ `0x80222D4`
- XOR scheme: `xor8(bytes[0..18]) == byte[19]` (same XOR logic as RX validator)

Wire format (20 bytes total):
- `byte 0` `0x01`
- `byte 1` `0x14` (length = data+checksum)
- `byte 2` counter `0x01` (OEM appears constant in this build)
- `byte 3` mode byte (`byte_20001E54`, OEM default `2`; semantics TBD)
- `byte 4` power level (0..15, mapped from assist selection depending on 3/5/9 gear model)
- `byte 5` flags (bitfield; sources recovered from `0x80222D4`, semantics partly TBD)
- `byte 6` display setting (`byte_20001E49`, OEM default `1`)
- `byte 7..8` wheel diameter x10 (big-endian, from `n160`)
- `byte 9` config byte `n3_1` (default `3`; bounded `3..0x18` in config path 0xC0)
- `byte 10` config byte `n3_2` (default `3`; bounded `0..5` in config path 0xC0)
- `byte 11` config byte `byte_20001E5B` (default `0`; set via config path 0xC0)
- `byte 12` speed limit (kph, from `n0x1FE / 10`)
- `byte 13` current limit (A, from `word_20001E50 / 1000`)
- `byte 14..15` battery threshold (mV/100, big-endian, from `word_20001E52 / 100`)
- `byte 16..17` reserved (0)
- `byte 18` status2 (low nibble from `byte_20001E5A & 0x0F`)
- `byte 19` XOR checksum

Flags byte notes (v2.5.1 evidence-backed sources, semantics marked TBD):
- bit7 is always set by the OEM.
- bit5 is sourced from `byte_20001DA9`, which is toggled by `sub_8012F5C` (UI callback) and sent over BLE via
  `sub_8011C40(case 1)`. Strong hypothesis: this is the headlight/light-enable bit.
- bit2 is only toggled when `byte_20001E65 != 0` and the filtered speed `word_20001DAC` exceeds the configured
  limit `n0x1FE` (both are `kph*10`). Semantics: likely a speed-limit enforcement/indicator flag.
- bit1 is sourced from `byte_20001DA6` which is manipulated by `sub_8012FC0` and affects assist mapping via
  `sub_801B5AC` (returns 11 when set). Semantics TBD (walk/cruise/special mode).

## Protocol C: 0x46/0x53 SOF, XOR, CR Terminator (Open-Firmware: `MOTOR_PROTO_AUTH_XOR_CR`)

Evidence (OEM v2.5.1, mode 3 path):
- Builders:
  - `sub_8023CA4` starts with `auth_command_buffer_append(70)` (`0x46`, 'F') and appends 2 bytes of control data.
  - `sub_8023BB0` starts with `auth_command_buffer_append(83)` (`0x53`, 'S') and appends 2 bytes of control data.
  - `finalize_auth_packet_sram_buffers` @ `0x8023E54`:
    - Appends a nonce byte (chosen so XOR checksum != `0x0D`).
    - Appends XOR checksum and then `0x0D` terminator.
    - XOR excludes the first byte.
- RX validator `sub_80234A0` @ `0x80234A0` checks XOR excluding the first byte.
- RX handler `sub_80234D8`:
  - If first byte is `0x46`, calls `APP_process_battery_status_packet` on the 6 bytes after SOF.

Wire format:
- Byte 0: `0x46` or `0x53` (SOF / opcode)
- Byte 1..N: data bytes (includes a nonce inserted by OEM builder)
- Byte (N+1): xor8(bytes[1..N])  (does not include byte0)
- Byte (N+2): `0x0D` (CR terminator)

### Protocol C: Display -> Motor control bytes (2-byte payload)

Evidence anchors:
- `sub_8023CA4` @ `0x8023CA4` builds `0x46` ('F') frames.
- `sub_8023BB0` @ `0x8023BB0` builds `0x53` ('S') frames.

In `0x46` ('F') frames, the first payload byte is a packed flag field:
- bits0..3: assist nibble (`sub_801B5AC() & 0x0F`), with 0 encoded as 0xF
- bit4: `byte_20001DA6` (special request flag; semantics TBD)
- bit7: `byte_20001DA9` (user-toggled flag; strong hypothesis: lights/headlight)
- bits5..6: not touched here (persist from previous value in OEM, but typically 0)

The second payload byte packs speed-limit and wheel code:
- bits3..7: `(speed_limit_kph - 20) & 0x1F` where `speed_limit_kph = (n0x1FE/10)`
- bits0..2: wheel code derived from `n160` (wheel diameter x10) using the 8-entry OEM table

Then the OEM finalizer appends:
- an extra nonce byte chosen so XOR(payload bytes) != `0x0D`
- XOR checksum (excludes the SOF byte)
- CR terminator

### Protocol C: Motor -> Display status bytes (6-byte payload on `0x46`)

Evidence anchor:
- `APP_process_battery_status_packet` @ `0x8023774`, called from `sub_80234D8` when SOF is `0x46`.

The motor sends 6 bytes after the SOF. OEM consumes:
- `p[0]`: used as a small integer scaled by 20 (0..5 -> 0..100). Likely an SoC/bar index.
- `p[1]`: current-like byte; OEM computes `mA = (p[1] / 3.0) * 1000`.
- `p[2..3]`: wheel period ms/rev (big-endian) used for speed math (`3.6 * wheel_mm / period_ms`).
- `p[4..5]`: present but not used in the core speed/current path in this handler.

## Protocol D: Short 5-Byte Frames (Heuristic) (Open-Firmware: `MOTOR_PROTO_V2_FIXED`)

Evidence (OEM v2.5.1, mode 2 path):
- The OEM maintains a short-frame subsystem with:
  - RX feed `motor_v2_rx_feed` @ `0x8022F14` (collects bytes into a small slot).
  - Response processing `sub_80227C8` @ `0x80227C8` checks small checksums like:
    - `b2 + b3 + 32 == b4` for one response class.
    - `b2 + b3 == b4` for another response class.
  - Multiple request builders enqueue short “commands” that begin with `0x11` and a byte opcode.

Open-firmware support status:
- We parse short v2 responses both opportunistically (5-byte heuristic) and with
  request-aligned capture when a v2 request is queued.
- We implement the OEM-style request sequencing loop (0x11 xx polls plus the
  0x16 0x1F 5-byte request) in `src/motor/motor_link.c` when the v2 protocol is
  active or during AUTO probing.
- Response decoding remains best-effort; the primary goal is robust capture and
  deterministic traces so we can map fields safely per controller variant.
