#!/usr/bin/env python3
"""Renode regression for regen capability, brake-blend, and thermal limiting.

Assumes Renode provides UART1 over PTY (BC280_UART1_PTY).
"""

import os
import sys
import time

from uart_client import ProtocolError, UARTClient

PORT = os.environ.get("BC280_UART1_PTY", "/tmp/uart1")

REGEN_STEP_W = 40
THERM_F_MIN_Q16 = 26214  # ~0.40


def wait_for_pty(path: str, timeout: float = 10.0) -> None:
    deadline = time.time() + timeout
    while time.time() < deadline:
        if os.path.exists(path):
            return
        time.sleep(0.1)
    raise FileNotFoundError(f"UART PTY not found at {path}")


def expect(cond: bool, msg: str) -> None:
    if not cond:
        raise AssertionError(msg)


def apply_q16(v: int, q16: int) -> int:
    return ((v * q16) + 0x8000) >> 16


def set_regen_support(client: UARTClient, supported: bool) -> None:
    caps = 0x01 | (0x02 if supported else 0x00)
    client.set_hw_caps(caps)
    cfg = client.config_get()
    if supported:
        cfg.flags |= 0x02
    else:
        cfg.flags &= ~0x02
    cfg.seq += 1
    client.config_stage(cfg)
    client.config_commit(reboot=False)


def main() -> int:
    try:
        wait_for_pty(PORT)
    except FileNotFoundError as e:
        sys.stderr.write(str(e) + "\n")
        return 1

    client = UARTClient(PORT, baud=115200, timeout=0.5)
    try:
        client.ping()

        # --- Unsupported regen path ---
        set_regen_support(client, False)
        status = client.set_regen(3, 5, allow_error=True)
        expect(status != 0, "set_regen should fail when regen unsupported")
        client.set_state(0, 0, 120, 90, 0, cadence_rpm=0, throttle_pct=0, brake=0, buttons=0)
        dbg = client.debug_state()
        expect(dbg.regen_supported == 0, "regen_supported should be 0 when unsupported")
        expect(dbg.regen_level == 0 and dbg.regen_brake_level == 0, "regen levels should be zeroed")
        expect(dbg.regen_cmd_power_w == 0 and dbg.regen_cmd_current_dA == 0, "regen commands should be zero")

        # --- Supported regen path ---
        set_regen_support(client, True)
        status = client.set_regen(2, 5)
        expect(status == 0, "set_regen should succeed when supported")

        client.set_state(0, 0, 120, 90, 0, cadence_rpm=0, throttle_pct=0, brake=0, buttons=0, ctrl_temp_dC=600)
        dbg = client.debug_state()
        expect(dbg.regen_supported == 1, "regen_supported should be 1 when enabled")
        expect(dbg.regen_level == 2, "regen_level mismatch")
        expect(dbg.regen_brake_level == 5, "regen_brake_level mismatch")
        expect(dbg.regen_cmd_power_w == 2 * REGEN_STEP_W, "regen base command mismatch")

        # Brake blend should favor brake_level.
        client.set_state(0, 0, 120, 90, 0, cadence_rpm=0, throttle_pct=0, brake=1, buttons=0, ctrl_temp_dC=600)
        dbg_brake = client.debug_state()
        expect(dbg_brake.regen_cmd_power_w == 5 * REGEN_STEP_W, "brake blend regen mismatch")

        # Thermal hard limit should reduce regen command.
        client.set_state(0, 0, 120, 90, 0, cadence_rpm=0, throttle_pct=0, brake=1, buttons=0, ctrl_temp_dC=900)
        dbg_hot = client.debug_state()
        expected = apply_q16(5 * REGEN_STEP_W, THERM_F_MIN_Q16)
        expect(dbg_hot.regen_cmd_power_w == expected, "regen thermal limit mismatch")

        print("PASS: regen capability, brake blend, thermal limiting")
        return 0
    except (ProtocolError, AssertionError) as e:
        sys.stderr.write(f"FAIL: {e}\n")
        return 1
    finally:
        client.close()


if __name__ == "__main__":
    raise SystemExit(main())
