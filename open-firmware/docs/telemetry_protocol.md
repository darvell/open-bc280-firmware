# BC280 Open Firmware Telemetry + Control Protocol (Spec v1)

Spec version: 1
Applies to: open-firmware UART debug protocol (UART1/UART2)
Telemetry payload version: 1

## 1. Framing

All frames use a fixed 0x55 start-of-frame byte and a 1-byte checksum.

```
SOF | CMD | LEN | PAYLOAD... | CHKSUM
```

- **SOF**: `0x55`
- **CMD**: command byte
- **LEN**: payload length in bytes (0..255)
- **PAYLOAD**: `LEN` bytes
- **CHKSUM**: bitwise-not of XOR over all prior bytes in the frame

Checksum definition:

```
uint8_t x = 0;
for each byte in [SOF..PAYLOAD]: x ^= byte;
CHKSUM = ~x;
```

## 2. Endianness and units

- Multibyte integer fields are **big-endian** unless noted.
- Signed fields use two's-complement in their declared width.
- Units are noted per field; common units:
  - `speed_dmph` = deci-mph (0.1 mph)
  - `batt_dV` = deci-volts (0.1 V)
  - `batt_dA` = deci-amps (0.1 A)
  - `ctrl_temp_dC` = deci-C (0.1 C)

## 3. Responses

- Most request/response pairs use `CMD | 0x80` for the response frame.
- Response payloads commonly start with a 1-byte **status**:
  - `0x00` = OK
  - `0xFC` = blocked by safety gating (e.g., config change while moving)
  - `0xFE` = invalid payload or range
  - `0xFF` = unsupported/unknown

Note: `0x81` is **overloaded**. It can be either:
- response to `0x01` ping (LEN=1, status byte), or
- asynchronous telemetry stream (LEN=22, payload versioned). Use `LEN` to disambiguate.

## 4. Commands (telemetry/control subset)

### 0x01 Ping
- **Request**: empty payload
- **Response**: `0x81` with payload `[status]` (LEN=1)

### 0x0A State dump (legacy)
- **Request**: empty payload
- **Response**: `0x8A` with 16-byte payload:
  - `ms` (`u32`)
  - `rpm` (`u16`)
  - `torque_raw` (`u16`)
  - `speed_dmph` (`u16`)
  - `soc` (`u8`)
  - `err` (`u8`)
  - `last_ms_lo16` (`u16`) lower 16 bits of last update timestamp
  - `reserved` (`u16`) currently zero

### 0x0C Set state (inputs)
- **Request**: minimum 8-byte payload:
  - `rpm` (`u16`)
  - `torque_raw` (`u16`)
  - `speed_dmph` (`u16`)
  - `soc` (`u8`)
  - `err` (`u8`)
- **Optional extensions** (if payload length is long enough):
  - bytes 8..9: `cadence_rpm` (`u16`)
  - byte 10: `throttle_pct` (`u8`)
  - byte 11: `brake` (`u8`, nonzero = braking)
  - byte 12: `buttons` (`u8`, raw button bitmask)
  - bytes 13..14: `power_w` (`u16`)
  - bytes 15..16: `batt_dV` (`i16`, deci-volts)
  - bytes 17..18: `batt_dA` (`i16`, deci-amps)
  - bytes 19..20: `ctrl_temp_dC` (`i16`, deci-C)
- **Response**: `0x8C` with payload `[status]`

### 0x0D Set stream period
- **Request**: `period_ms` (`u16`)
  - `0` disables streaming
- **Response**: `0x8D` with payload `[status]`

### 0x81 Telemetry stream (v1)
Asynchronous telemetry frames emitted when streaming is enabled.

Payload layout (LEN=22):
- byte 0: `version` (`u8`, currently `1`)
- byte 1: `size` (`u8`, currently `22`)
- bytes 2..5: `ms` (`u32`)
- bytes 6..7: `speed_dmph` (`u16`, 0.1 mph)
- bytes 8..9: `cadence_rpm` (`u16`)
- bytes 10..11: `power_w` (`u16`)
- bytes 12..13: `batt_dV` (`i16`, 0.1 V)
- bytes 14..15: `batt_dA` (`i16`, 0.1 A)
- bytes 16..17: `ctrl_temp_dC` (`i16`, 0.1 C)
- byte 18: `assist_mode` (`u8`)
- byte 19: `profile_id` (`u8`)
- byte 20: `virtual_gear` (`u8`)
- byte 21: `flags` (`u8`)
  - bit0: brake
  - bit1: walk active

## 5. Versioning

- Telemetry stream frames are explicitly versioned via `payload[0]` and `payload[1]`.
- When adding fields, increment the telemetry version and update this spec.

## 6. References

- Full command list and extended structs: `open-firmware/README.md` (Debug protocol section).
- Telemetry log parsing helper: `scripts/renode/parse_uart_log.py`.
