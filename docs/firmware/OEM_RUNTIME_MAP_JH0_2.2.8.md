# OEM Runtime Map (BC280 JH0 App v2.2.8)

Source DB: `BC280_Combined_Firmware_0.2.0_2.2.8-JH0.bin` (IDA)

This note captures the three runtime anchors requested for JH0:
- `APP_process_motor_response_packet` (`0x80218A4`)
- `motor_uart_rx_frame_dispatch` (`0x8021248`)
- `sub_801EEF4` (`0x801EEF4`)

The goal is to preserve behavioral parity where startup-to-ride stability depends on
these paths.

## 1) `APP_process_motor_response_packet` (`0x80218A4`)

Called from:
- `motor_uart_rx_frame_dispatch` cmd `1` path only.

Input:
- 10-byte payload (`p[0..9]`) from the motor status frame.

Behavior:
1. Copies up to 10 bytes into `byte_20002FBA[]`.
2. Decodes `p[0]` with fixed priority into global `n2`:
   - bit1 -> `2`
   - bit3 -> `6`
   - bit0 -> `7`
   - bit5 -> `8`
   - bit4 -> `9`
   - bit6 -> `20`
   - else -> `0`
3. Stores two direct flags from `p[0]`:
   - bit2 -> `byte_20001D41`
   - bit7 -> `byte_20001D43`
4. Parses current-like field from `p[2..3]` (big-endian):
   - if bit14 (`0x4000`) set: `mA = (raw & 0x3FFF) * 0.1 * 1000`
   - else: `mA = (raw & 0x3FFF) * 1000`
   - writes to `word_20001D2A`.
5. Parses period from `p[5..6]` (big-endian ms/rev):
   - if `< 3000`, computes speed from wheel circumference and clamps to `99.9`.
   - stores deci-kph style value in `word_20002FB2`.
6. Updates smoothing delta `word_20002FB6` relative to `word_20002FB4`.
7. If `byte_20001D31 == 1`, copies `p[7]` to `n100`.
8. Else sets `word_20001D28 = 1000 * byte_20001E20`.

Known consumer behavior for `byte_20001D41`:
- `sub_801B1B8()` returns sentinel level `10` when this flag is set.
- `sub_8012EA0(1)` short-circuits assist increment when this flag is set.

Known consumer behavior for `byte_20001D43`:
- No confirmed read sites in this JH0 build (write-only in current xref set).

## 2) `motor_uart_rx_frame_dispatch` (`0x8021248`)

Framed command dispatcher for UART2 receive queue.

Observed command handling:
- `1`:
  - calls `APP_process_motor_response_packet(v5+5, 10, motor_state)`
  - sets `byte_20001F42 = 1`
- `166`:
  - reads 64 bytes from flash region `0x003FF000` (`4190208`)
  - responds with cmd `166` and 64-byte payload
- `167`:
  - writes 4 bytes to `0x003FF000 + 4*index`
  - calls reload helper (`sub_801B380`)
  - responds with success
- `168`:
  - variable-size write to slot table (`dword_8026234[n+11]`)
  - bounded by `n <= 0x10`
  - persists, reloads, and ACKs success/fail
- `169`:
  - reads slot data from `dword_8026234[n+11]`
  - returns variable payload by slot type
- `170`:
  - forces state transition through `sub_801EEF4(35)`
  - calls `handle_display_mode_and_assist_level(arg)`
- `192`:
  - applies large settings blob with range checks
  - writes many globals and persists via `sub_801AA2C(1)`
  - calls `sub_8021D10(1)`
- `194`:
  - sends config/status snapshot through `sub_8021BD8(1)`

## 3) `sub_801EEF4` (`0x801EEF4`) power/key transitions

This function is the central power/UI state machine.

Critical power-related states:
- state `0`:
  - schedules delayed transition to state `1`.
- state `1`:
  - drives PB1 high via `sub_8018E18(0x40010C00, 2)`.
- state `5`:
  - performs shutdown prep and calls `system_prepare_poweroff_sequence(1)`.
- state `6`:
  - drives PB1 low via `sub_8018E14(0x40010C00, 2)`.
  - blanks display/backlight.
  - schedules delayed return to state `1`.
  - reinitializes dispatcher (`APP_Dispatcher_Init`).

Ride/runtime states used after power-up:
- state `9`:
  - normal riding loop setup (assist key handlers, periodic tasks, timeout to state `5`).
- state `10` and higher menu states:
  - settings/menu flow, with periodic timeout back to state `5`.

Implication for open-firmware parity:
- PB1 is not a one-time init pin in OEM behavior.
- It is actively sequenced by runtime state transitions (`6 -> 1` pulse behavior).
- If this sequencing is missing or mistimed, real hardware can fail to stay in the
  expected run state.
