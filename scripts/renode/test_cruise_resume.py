#!/usr/bin/env python3
"""Renode regression for cruise smart resume gating.

Scenarios:
- cancel cruise with brake
- attempt resume with speed out of range -> blocked + UI trace reason
- attempt resume within speed delta -> resumes successfully

Requires Renode UART1 PTY at $BC280_UART1_PTY (defaults /tmp/uart1).
"""

import os
import sys
import time

from uart_client import ProtocolError, UARTClient

PORT = os.environ.get("BC280_UART1_PTY", "/tmp/uart1")
OUTDIR = os.environ.get("BC280_RENODE_OUTDIR") or os.path.abspath(
    os.path.join(os.path.dirname(__file__), "..", "..", "open-firmware", "renode", "output")
)
UART_LOG = os.path.join(OUTDIR, "uart1_tx.log")

CRUISE_BUTTON = 0x80
CRUISE_SPEED_SELECT = 0x08

CRUISE_STATE_OFF = 0
CRUISE_STATE_SPEED = 1


def wait_for_pty(path: str, timeout: float = 5.0) -> None:
    deadline = time.time() + timeout
    while time.time() < deadline:
        if os.path.exists(path):
            return
        time.sleep(0.1)
    raise FileNotFoundError(f"UART PTY not found at {path}")


def expect(cond: bool, msg: str) -> None:
    if not cond:
        raise AssertionError(msg)


def reset_uart_log(path: str) -> None:
    try:
        if os.path.exists(path):
            os.remove(path)
    except Exception:
        pass


def read_trace_lines() -> list:
    if not os.path.exists(UART_LOG):
        return []
    with open(UART_LOG, "r", errors="ignore") as f:
        return [ln.strip() for ln in f.readlines() if "[TRACE]" in ln]


def main() -> int:
    try:
        wait_for_pty(PORT)
    except FileNotFoundError as e:
        sys.stderr.write(str(e) + "\n")
        return 1

    reset_uart_log(UART_LOG)
    client = UARTClient(PORT, baud=115200, timeout=0.5)
    try:
        client.ping()

        # Engage cruise in speed-hold at 16.0 mph.
        client.set_state(0, 80, 160, 90, 0, cadence_rpm=0, throttle_pct=10, brake=0, buttons=0x00)
        client.set_state(0, 80, 160, 90, 0, cadence_rpm=0, throttle_pct=10, brake=0,
                         buttons=CRUISE_BUTTON | CRUISE_SPEED_SELECT)
        client.set_state(0, 0, 160, 90, 0, cadence_rpm=0, throttle_pct=0, brake=0, buttons=0x00)
        dbg = client.debug_state()
        expect(dbg.cruise_state == CRUISE_STATE_SPEED, "cruise engage failed")

        # Brake cancels cruise and arms resume.
        client.set_state(0, 0, 160, 90, 0, cadence_rpm=0, throttle_pct=0, brake=1, buttons=0x00)
        dbg_brake = client.debug_state()
        expect(dbg_brake.cruise_state == CRUISE_STATE_OFF, "cruise should clear on brake")

        # Attempt resume with speed out of range (blocked).
        client.set_state(0, 0, 300, 90, 0, cadence_rpm=0, throttle_pct=0, brake=0,
                         buttons=CRUISE_BUTTON)
        client.set_state(0, 0, 300, 90, 0, cadence_rpm=0, throttle_pct=0, brake=0, buttons=0x00)
        dbg_block = client.debug_state()
        expect(dbg_block.cruise_state == CRUISE_STATE_OFF, "resume should stay off when blocked")
        time.sleep(0.3)
        lines = read_trace_lines()
        expect(any("resume=speed" in ln for ln in lines),
               "UI trace missing resume block reason (speed)")

        # Attempt resume within speed window (allowed).
        client.set_state(0, 0, 165, 90, 0, cadence_rpm=0, throttle_pct=0, brake=0,
                         buttons=CRUISE_BUTTON)
        client.set_state(0, 0, 165, 90, 0, cadence_rpm=0, throttle_pct=0, brake=0, buttons=0x00)
        dbg_resume = client.debug_state()
        expect(dbg_resume.cruise_state == CRUISE_STATE_SPEED, "resume should re-engage cruise")

        print("PASS: cruise smart resume gating + UI trace reason")
        return 0
    except (ProtocolError, AssertionError) as e:
        sys.stderr.write(f"FAIL: {e}\n")
        return 1
    finally:
        client.close()


if __name__ == "__main__":
    raise SystemExit(main())
