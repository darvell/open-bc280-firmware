#!/usr/bin/env python3
"""Renode test for MENU+POWER short press (should NOT reboot)."""

import os
import sys
import time

from uart_client import ProtocolError, UARTClient

PORT = os.environ.get("BC280_UART1_PTY", "/tmp/uart1")

BUTTON_MENU = 0x04
BUTTON_POWER = 0x08
BUTTON_COMBO = BUTTON_MENU | BUTTON_POWER

SHORT_PRESS_S = 0.2


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


def wait_for_ping(client: UARTClient, tries: int = 30, delay: float = 0.15) -> None:
    for _ in range(tries):
        try:
            client.ping()
            return
        except Exception:
            time.sleep(delay)
    raise ProtocolError("ping did not recover")


def ensure_min_uptime(client: UARTClient, min_ms: int = 1200) -> int:
    st = client.debug_state()
    if st.ms < min_ms:
        wait_s = (min_ms - st.ms) / 1000.0 + 0.2
        time.sleep(wait_s)
        st = client.debug_state()
    return st.ms


def main() -> int:
    try:
        wait_for_pty(PORT)
    except FileNotFoundError as e:
        sys.stderr.write(str(e) + "\n")
        return 1

    client = UARTClient(PORT, baud=115200, timeout=0.5)
    try:
        wait_for_ping(client)
        print("  [OK] Firmware responding to ping")

        ms_before = ensure_min_uptime(client)
        print(f"  [OK] Uptime before short press: {ms_before} ms")

        client.set_state(rpm=0, torque=0, speed_dmph=0, soc=80, err=0, buttons=BUTTON_COMBO)
        time.sleep(SHORT_PRESS_S)
        client.set_state(rpm=0, torque=0, speed_dmph=0, soc=80, err=0, buttons=0)
        print("  [OK] Sent MENU+POWER short press")

        time.sleep(0.5)
        wait_for_ping(client)
        print("  [OK] Firmware still responding")

        ms_after = client.debug_state().ms
        print(f"  [OK] Uptime after short press: {ms_after} ms")
        expect(ms_after >= ms_before, "Unexpected reboot after short press")

        print("PASS: recovery combo short press does not reboot")
        return 0

    except (AssertionError, ProtocolError) as e:
        sys.stderr.write(f"FAIL: {e}\n")
        return 1
    finally:
        client.close()


if __name__ == "__main__":
    raise SystemExit(main())
