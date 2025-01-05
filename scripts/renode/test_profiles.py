#!/usr/bin/env python3
"""Renode regression for assist profiles + persistence.

Flow:
- ping
- set profile via command (persist)
- assert debug state reflects id + caps
- read config_get to confirm persisted profile_id
- switch via buttons bits in set_state (quick switch path)
- assert debounce still allows change and caps follow
- invalid profile id should return nonzero status

Usage:
  BC280_UART1_PTY=/tmp/uart1 ./scripts/renode/test_profiles.py
"""

import os
import sys
import time

from uart_client import ProtocolError, UARTClient

PORT = os.environ.get("BC280_UART1_PTY", "/tmp/uart1")


def expect(cond: bool, msg: str) -> None:
    if not cond:
        raise AssertionError(msg)


def main() -> int:
    for _ in range(50):
        if os.path.exists(PORT):
            break
        time.sleep(0.1)
    if not os.path.exists(PORT):
        sys.stderr.write(f"UART PTY not found at {PORT}\n")
        return 1

    client = UARTClient(PORT, baud=115200, timeout=0.5)
    try:
        client.ping()

        # Set profile to cargo (id=2) with persistence.
        client.set_profile(2, persist=True)
        dbg = client.debug_state()
        expect(dbg.profile_id == 2, "profile_id not applied")
        expect(dbg.cap_power_w == 650, "cap_power_w mismatch for cargo")
        expect(dbg.cap_current_dA == 200, "cap_current_dA mismatch for cargo")
        expect(dbg.cap_speed_dmph == 220, "cap_speed cap mismatch for cargo")

        cfg = client.config_get()
        expect(cfg.profile_id == 2, "config did not persist profile")

        # Quick-switch via buttons low bits -> trail (id=1).
        client.set_state(rpm=0, torque=0, speed_dmph=150, soc=80, err=0,
                         buttons=0x01)
        time.sleep(0.12)  # respect firmware debounce (~100 ms)
        dbg2 = client.debug_state()
        expect(dbg2.profile_id == 1, "button-based profile switch failed")
        expect(dbg2.cap_power_w == 750, "cap_power_w mismatch for trail")
        expect(dbg2.curve_power_w > 0, "curve_power_w missing")

        # Invalid profile id should fail.
        failed = False
        try:
            client.set_profile(9, persist=False)
        except ProtocolError:
            failed = True
        expect(failed, "invalid profile id should error")

        print("PASS: profiles switch + caps + persistence")
        return 0
    except (AssertionError, ProtocolError) as e:
        sys.stderr.write(f"FAIL: {e}\n")
        return 1
    finally:
        client.close()


if __name__ == "__main__":
    raise SystemExit(main())
