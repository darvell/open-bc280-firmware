#!/usr/bin/env python3
"""Renode regression for cruise control (speed-hold + power-hold).

Scenarios:
- engage cruise in speed-hold mode and verify output adjusts with speed error
- brake cancels cruise and logs a cancel event with reason
- trace lines indicate cruise mode + setpoint, and clear on cancel

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

EVT_CRUISE_EVENT = 6
CRUISE_CANCEL_BRAKE = 0x11


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

    # Reset UART log for deterministic trace assertions.
    try:
        if os.path.exists(UART_LOG):
            os.remove(UART_LOG)
    except Exception:
        pass

    client = UARTClient(PORT, baud=115200, timeout=0.5)
    try:
        client.ping()

        # Establish baseline (no cruise).
        client.set_state(0, 80, 160, 90, 0, cadence_rpm=0, throttle_pct=10, brake=0, buttons=0x00)

        # Engage cruise (speed-hold) via button edge.
        client.set_state(0, 80, 160, 90, 0, cadence_rpm=0, throttle_pct=10, brake=0,
                         buttons=CRUISE_BUTTON | CRUISE_SPEED_SELECT)
        client.set_state(0, 0, 160, 90, 0, cadence_rpm=0, throttle_pct=0, brake=0, buttons=0x00)

        dbg = client.debug_state()
        expect(dbg.cruise_state == CRUISE_STATE_SPEED, f"expected speed-hold cruise, got {dbg.cruise_state}")
        baseline_cmd = dbg.cmd_power_w
        expect(baseline_cmd > 0, "baseline cruise command should be >0")

        # Speed below setpoint -> command should rise (or saturate).
        client.set_state(0, 0, 140, 90, 0, cadence_rpm=0, throttle_pct=0, brake=0, buttons=0x00)
        dbg_low = client.debug_state()
        expect(dbg_low.cmd_power_w >= baseline_cmd,
               f"cruise should increase power on low speed ({dbg_low.cmd_power_w} < {baseline_cmd})")

        # Speed above setpoint -> command should fall (or saturate).
        client.set_state(0, 0, 180, 90, 0, cadence_rpm=0, throttle_pct=0, brake=0, buttons=0x00)
        dbg_high = client.debug_state()
        expect(dbg_high.cmd_power_w <= dbg_low.cmd_power_w,
               "cruise should reduce power on high speed")

        # Brake cancel + event log reason.
        ev_before = client.event_log_summary()
        client.set_state(0, 0, 180, 90, 0, cadence_rpm=0, throttle_pct=0, brake=1, buttons=0x00)
        dbg_brake = client.debug_state()
        expect(dbg_brake.cruise_state == CRUISE_STATE_OFF, "cruise should clear on brake")
        expect(dbg_brake.cmd_power_w == 0, "cruise output should be zero on brake")

        ev_after = client.event_log_summary()
        new_count = max(0, ev_after.count - ev_before.count)
        recs = client.event_log_read(ev_before.count, limit=max(1, min(8, new_count)))
        cruise_cancel = [r for r in recs if r.type == EVT_CRUISE_EVENT and r.flags == CRUISE_CANCEL_BRAKE]
        expect(cruise_cancel, "missing cruise cancel event with brake reason")

        # Trace lines should include cruise mode/setpoint and clear when canceled.
        lines = read_trace_lines()
        expect(lines, "no trace lines found")
        expect(any("cruise=SPD" in ln and "set=" in ln for ln in lines),
               "trace missing cruise speed-hold indicator")
        expect(any("cruise=OFF" in ln for ln in lines),
               "trace missing cruise-off indicator after cancel")

        print("PASS: cruise control speed-hold + brake cancel + trace")
        return 0
    except (ProtocolError, AssertionError) as e:
        sys.stderr.write(f"FAIL: {e}\n")
        return 1
    finally:
        client.close()


if __name__ == "__main__":
    raise SystemExit(main())
