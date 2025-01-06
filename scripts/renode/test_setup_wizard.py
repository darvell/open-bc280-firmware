#!/usr/bin/env python3
"""Renode regression for the on-device setup wizard.

Validates:
- Wizard traces expose step + selection values.
- Invalid config (out-of-range wheel) does not commit and reports error.
- Successful wizard commit updates config blob fields.
"""

import os
import sys
import time
from typing import Dict, List

from uart_client import UARTClient, ProtocolError

PORT = os.environ.get("BC280_UART1_PTY", "/tmp/uart1")
OUTDIR = os.environ.get("BC280_RENODE_OUTDIR") or os.path.abspath(
    os.path.join(os.path.dirname(__file__), "..", "..", "open-firmware", "renode", "output")
)
UART_LOG = os.path.join(OUTDIR, "uart1_tx.log")

WIZ_BTN_UP = 0x10
WIZ_BTN_DOWN = 0x20
WIZ_BTN_BACK = 0x40
WIZ_BTN_NEXT = 0x80
WIZ_BTN_START = WIZ_BTN_BACK | WIZ_BTN_NEXT

WIZ_STEP_WHEEL = 0
WIZ_STEP_UNITS = 1
WIZ_STEP_BUTTONS = 2
WIZ_STEP_PROFILE = 3
WIZ_STEP_DONE = 4

WIZ_WHEEL_STEP = 50
WIZ_WHEEL_MIN = 100
WIZ_WHEEL_MAX = 6000
BUTTON_MAP_MAX = 2


def expect(cond: bool, msg: str) -> None:
    if not cond:
        raise AssertionError(msg)


def wait_for_pty(path: str, timeout_s: float = 10.0) -> None:
    deadline = time.time() + timeout_s
    while time.time() < deadline:
        if os.path.exists(path):
            return
        time.sleep(0.1)
    raise FileNotFoundError(f"UART PTY not found at {path}")


def parse_trace_log(path: str) -> List[Dict[str, int]]:
    entries: List[Dict[str, int]] = []
    if not os.path.exists(path):
        return entries
    with open(path, "r", errors="ignore") as f:
        for raw in f:
            line = raw.strip()
            if not line.startswith("[TRACE] wizard"):
                continue
            parts = line.split()
            kv: Dict[str, int] = {}
            for token in parts[2:]:
                if "=" not in token:
                    continue
                key, val = token.split("=", 1)
                if not val:
                    continue
                try:
                    kv[key] = int(val)
                except ValueError:
                    continue
            if "step" in kv and "wheel" in kv:
                entries.append(kv)
    return entries


def wait_for_traces(path: str, min_entries: int, timeout_s: float = 3.0) -> List[Dict[str, int]]:
    deadline = time.time() + timeout_s
    entries: List[Dict[str, int]] = []
    while time.time() < deadline:
        entries = parse_trace_log(path)
        if len(entries) >= min_entries:
            return entries
        time.sleep(0.05)
    return entries


def press(client: UARTClient, buttons: int) -> None:
    client.set_state(
        rpm=0,
        torque=0,
        speed_dmph=0,
        soc=0,
        err=0,
        cadence_rpm=0,
        throttle_pct=0,
        brake=0,
        buttons=buttons,
        power_w=0,
        batt_dV=0,
        batt_dA=0,
        ctrl_temp_dC=0,
    )
    client.set_state(
        rpm=0,
        torque=0,
        speed_dmph=0,
        soc=0,
        err=0,
        cadence_rpm=0,
        throttle_pct=0,
        brake=0,
        buttons=0,
        power_w=0,
        batt_dV=0,
        batt_dA=0,
        ctrl_temp_dC=0,
    )


def clamp(val: int, mn: int, mx: int) -> int:
    return max(mn, min(mx, val))


def main() -> int:
    try:
        wait_for_pty(PORT)
    except FileNotFoundError as e:
        sys.stderr.write(str(e) + "\n")
        return 1

    try:
        if os.path.exists(UART_LOG):
            os.remove(UART_LOG)
    except Exception:
        pass

    client = UARTClient(PORT, baud=115200, timeout=0.5)
    try:
        client.ping()
        start_cfg = client.config_get()

        # Enter wizard (Walk + Cruise).
        press(client, WIZ_BTN_START)
        entries = wait_for_traces(UART_LOG, min_entries=1, timeout_s=2.0)
        expect(entries, "no wizard traces found (RENODE_TEST build required)")
        last = entries[-1]
        expect(last.get("step") == WIZ_STEP_WHEEL, "wizard did not start at wheel step")
        expect(last.get("active") == 1, "wizard not active after start")

        # Drive wheel below valid minimum, then attempt commit -> expect error.
        wizard_wheel = start_cfg.wheel_mm
        target_invalid = 400
        steps_down = max(0, (wizard_wheel - target_invalid) // WIZ_WHEEL_STEP)
        for _ in range(steps_down):
            press(client, WIZ_BTN_DOWN)
            wizard_wheel = clamp(wizard_wheel - WIZ_WHEEL_STEP, WIZ_WHEEL_MIN, WIZ_WHEEL_MAX)

        for _ in range(4):
            press(client, WIZ_BTN_NEXT)
        press(client, WIZ_BTN_NEXT)  # commit attempt
        entries = wait_for_traces(UART_LOG, min_entries=len(entries) + 1, timeout_s=2.0)
        last = entries[-1]
        expect(last.get("step") == WIZ_STEP_DONE, "wizard not at done step")
        expect(last.get("active") == 1, "wizard unexpectedly exited on invalid commit")
        expect(last.get("err", 0) != 0, "wizard did not report validation error")

        after_invalid = client.config_get()
        expect(after_invalid.wheel_mm == start_cfg.wheel_mm, "invalid wizard commit modified config")

        # Back to wheel step and set valid values.
        for _ in range(4):
            press(client, WIZ_BTN_BACK)
        while wizard_wheel < 500:
            press(client, WIZ_BTN_UP)
            wizard_wheel = clamp(wizard_wheel + WIZ_WHEEL_STEP, WIZ_WHEEL_MIN, WIZ_WHEEL_MAX)

        press(client, WIZ_BTN_NEXT)  # units
        press(client, WIZ_BTN_UP)
        units_expected = 1 if start_cfg.units == 0 else 0

        press(client, WIZ_BTN_NEXT)  # button map
        press(client, WIZ_BTN_UP)
        button_map_expected = (start_cfg.button_map + 1) % (BUTTON_MAP_MAX + 1)

        press(client, WIZ_BTN_NEXT)  # profile
        for _ in range(3):
            press(client, WIZ_BTN_UP)
        profile_expected = (start_cfg.profile_id + 3) % 5

        press(client, WIZ_BTN_NEXT)  # done
        press(client, WIZ_BTN_NEXT)  # commit success

        updated = client.config_get()
        expect(updated.wheel_mm == wizard_wheel, "wizard wheel_mm not committed")
        expect(updated.units == units_expected, "wizard units not committed")
        expect(updated.button_map == button_map_expected, "wizard button_map not committed")
        expect(updated.profile_id == profile_expected, "wizard profile not committed")

        print("PASS: setup wizard config + validation behavior")
        return 0
    except (AssertionError, ProtocolError) as e:
        sys.stderr.write(f"FAIL: {e}\n")
        return 1
    finally:
        client.close()


if __name__ == "__main__":
    raise SystemExit(main())
