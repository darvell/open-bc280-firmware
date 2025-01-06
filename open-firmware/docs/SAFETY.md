# Safety Invariants (Open Firmware)

This firmware is intended to be safe, predictable, and OEM‑parity or better.

## Core invariants
- Brake input always wins: power commands go to zero within one control tick.
- Outputs are hard‑clamped by configured caps (speed/current/power) before being sent.
- Thermal/sag derate is monotonic (no re‑enable oscillation) and never increases power.
- Sensor loss or invalid inputs degrade to a safe state (no unexpected assist).
- Watchdog is active in normal operation and must be fed on schedule.
- Bootloader entry path is never blocked; bootloader flag remains writable.

## Config safety gating
- Configuration writes (staging/commit + tuning commands) are blocked while moving.
- Current gate: speed ≤ 1.0 mph (10 dMPH). Requests above this return status 0xFC.

## Debug surface (v1.0)
- Debug UART commands are enabled to validate behaviors and test parity.
- Production release policy should disable or gate these commands.

## Boot safety
- On restart, when bootloader entry is required, set the bootloader flag at
  SPI flash address 0x0030_0000 + 0x3FF080 and jump via the OEM bootloader.
