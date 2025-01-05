#!/usr/bin/env python3
"""Renode regression for quick actions + lock screen behavior.

Validates:
- long-press quick actions trigger and short presses do not
- lock screen masks disallowed buttons while allowing preselected ones
- brake cancels cruise even when lock is active
"""

import dataclasses
import os
import sys
import time

from uart_client import ProtocolError, UARTClient

PORT = os.environ.get("BC280_UART1_PTY", "/tmp/uart1")

CRUISE_BUTTON = 0x80
GEAR_UP = 0x10
GEAR_DOWN = 0x20
PAGE_RAW = 0x04

CFG_FLAG_QA_CRUISE = 1 << 2
CFG_FLAG_QA_PROFILE = 1 << 3
CFG_FLAG_QA_CAPTURE = 1 << 4

LOCK_ENABLE = 1 << 0
LOCK_ALLOW_GEAR = 1 << 2
LOCK_ALLOW_CRUISE = 1 << 3

QA_NONE = 0
QA_TOGGLE_CRUISE = 1
QA_CYCLE_PROFILE = 2
QA_TOGGLE_CAPTURE = 3


def expect(cond: bool, msg: str) -> None:
    if not cond:
        raise AssertionError(msg)


def wait_for_pty(path: str, timeout_s: float = 5.0) -> None:
    deadline = time.time() + timeout_s
    while time.time() < deadline:
        if os.path.exists(path):
            return
        time.sleep(0.1)
    raise FileNotFoundError(f"UART PTY not found at {path}")


def pulse_short(client: UARTClient, rpm: int, tq: int, spd: int, soc: int, buttons: int) -> None:
    client.set_state(rpm, tq, spd, soc, 0, cadence_rpm=0, throttle_pct=12, brake=0, buttons=buttons)
    time.sleep(0.1)
    client.set_state(rpm, tq, spd, soc, 0, cadence_rpm=0, throttle_pct=12, brake=0, buttons=0)


def pulse_long(client: UARTClient, rpm: int, tq: int, spd: int, soc: int, buttons: int) -> None:
    client.set_state(rpm, tq, spd, soc, 0, cadence_rpm=0, throttle_pct=12, brake=0, buttons=buttons)
    time.sleep(0.9)
    client.set_state(rpm, tq, spd, soc, 0, cadence_rpm=0, throttle_pct=12, brake=0, buttons=buttons)
    time.sleep(0.1)
    client.set_state(rpm, tq, spd, soc, 0, cadence_rpm=0, throttle_pct=12, brake=0, buttons=0)


def main() -> int:
    try:
        wait_for_pty(PORT)
    except FileNotFoundError as e:
        sys.stderr.write(str(e) + "\n")
        return 1

    client = UARTClient(PORT, baud=115200, timeout=0.5)
    try:
        client.ping()

        # Enable quick actions, disable lock.
        cfg = client.config_get()
        cfg = dataclasses.replace(cfg)
        cfg.flags |= CFG_FLAG_QA_CRUISE | CFG_FLAG_QA_PROFILE | CFG_FLAG_QA_CAPTURE
        cfg.button_flags = 0
        client.config_stage(cfg)
        client.config_commit(reboot=False)
        time.sleep(0.1)

        rpm = 0
        tq = 80
        spd = 120  # 12 mph
        soc = 90

        base = client.debug_state()
        qa_before = base.quick_action_last

        # Short press should not trigger quick action.
        pulse_short(client, rpm, tq, spd, soc, GEAR_UP)
        dbg_short = client.debug_state()
        expect(dbg_short.quick_action_last == qa_before, "short press triggered quick action")

        # Long press gear up -> cycle profile.
        profile_before = dbg_short.profile_id
        pulse_long(client, rpm, tq, spd, soc, GEAR_UP)
        dbg_profile = client.debug_state()
        expect(dbg_profile.quick_action_last == QA_CYCLE_PROFILE, "profile quick action not recorded")
        expect(dbg_profile.profile_id != profile_before, "profile did not change on quick action")

        # Long press gear down -> toggle capture.
        cap_before = client.bus_capture_summary().enabled
        pulse_long(client, rpm, tq, spd, soc, GEAR_DOWN)
        cap_after = client.bus_capture_summary().enabled
        dbg_capture = client.debug_state()
        expect(dbg_capture.quick_action_last == QA_TOGGLE_CAPTURE, "capture quick action not recorded")
        expect(cap_after != cap_before, "capture enable did not toggle")

        # Long press cruise -> toggle cruise.
        client.set_state(rpm, tq, spd, soc, 0, cadence_rpm=0, throttle_pct=12, brake=1, buttons=0)
        client.set_state(rpm, tq, spd, soc, 0, cadence_rpm=0, throttle_pct=12, brake=0, buttons=0)
        pulse_long(client, rpm, tq, spd, soc, CRUISE_BUTTON)
        dbg_cruise = client.debug_state()
        expect(dbg_cruise.quick_action_last == QA_TOGGLE_CRUISE, "cruise quick action not recorded")
        expect(dbg_cruise.cruise_state != 0, "cruise did not engage via quick action")

        # Enable lock: allow gear + cruise, disallow page/profile bits.
        cfg = client.config_get()
        cfg = dataclasses.replace(cfg)
        cfg.button_flags = LOCK_ENABLE | LOCK_ALLOW_GEAR | LOCK_ALLOW_CRUISE
        client.config_stage(cfg)
        client.config_commit(reboot=False)
        time.sleep(0.1)

        # Disallowed page button should be masked while riding.
        client.set_state(rpm, tq, spd, soc, 0, cadence_rpm=0, throttle_pct=12, brake=0, buttons=PAGE_RAW)
        time.sleep(0.1)
        dbg_page = client.debug_state()
        expect((dbg_page.buttons & PAGE_RAW) == 0, "lock did not mask page button")

        # Allowed gear button should still change gear.
        gear_before = dbg_page.virtual_gear
        pulse_short(client, rpm, tq, spd, soc, GEAR_UP)
        dbg_gear = client.debug_state()
        expect(dbg_gear.virtual_gear != gear_before, "gear input ignored while locked")

        # Safety: brake cancels cruise while locked.
        pulse_short(client, rpm, tq, spd, soc, CRUISE_BUTTON)
        dbg_on = client.debug_state()
        expect(dbg_on.cruise_state != 0, "cruise did not engage under lock")
        client.set_state(rpm, tq, spd, soc, 0, cadence_rpm=0, throttle_pct=12, brake=1, buttons=0)
        dbg_brake = client.debug_state()
        expect(dbg_brake.cruise_state == 0, "brake failed to cancel cruise while locked")

        print("PASS: quick actions + lock screen guardrails")
        return 0
    except (AssertionError, ProtocolError) as e:
        sys.stderr.write(f"FAIL: {e}\n")
        return 1
    finally:
        client.close()


if __name__ == "__main__":
    raise SystemExit(main())
