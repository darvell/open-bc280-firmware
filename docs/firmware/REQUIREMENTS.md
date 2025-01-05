# Functional Requirements (v1.0)

## Baseline behavior
- Match OEM behavior for normal ride conditions: speed reporting, assist response,
  cadence/torque response, and error handling.
- Use OEM‑compatible boot path and memory layout.
- Recovery entry (button combo) must preserve the OEM bootloader path; never bypass or replace it.
- Recovery entry uses MENU+POWER long-press to set the bootloader flag and reboot.

## Safety & limits
- Enforce hard caps for current/power/speed from `src/config/config.h`.
- Brake cancels assist immediately.
- Derate on thermal/sag conditions with smooth recovery.

## Configurability
- Allow users to tune profiles, gears, cadence bias, regen levels, and drive mode
  within safe ranges.
- All tunables are validated and clamped before apply.
- Config writes are only allowed when speed ≤ 1.0 mph (10 dMPH).

## Test expectations
- Host tests cover edge cases (brake edge, sag/thermal derate, config validation).
- Sim/host runs should be deterministic for a fixed input trace.

## Debug & tooling (v1.0)
- UART debug protocol remains enabled for validation and parity checks.
- Future releases should gate or disable debug commands for production builds.
