#!/usr/bin/env python3
"""Renode regression for assist curve engine (piecewise-linear, fixed-point).

Verifies:
- speed curve interpolation clamps at ends and interpolates between points
- cadence multiplier scales power
- speed cap zeros assist
- debug state exposes curve-derived fields

Usage:
  BC280_UART1_PTY=/tmp/uart1 ./scripts/renode/test_assist_curve.py
"""

import os
import sys
import time
from typing import List, Tuple

from uart_client import ProtocolError, UARTClient

PORT = os.environ.get("BC280_UART1_PTY", "/tmp/uart1")


def expect(cond: bool, msg: str) -> None:
    if not cond:
        raise AssertionError(msg)


def interp_linear(x: int, pts: List[Tuple[int, int]]) -> int:
    if not pts:
        return 0
    if x <= pts[0][0]:
        return pts[0][1]
    if x >= pts[-1][0]:
        return pts[-1][1]
    for i in range(1, len(pts)):
        x1, y1 = pts[i]
        if x <= x1:
            x0, y0 = pts[i - 1]
            dx = x1 - x0
            if dx == 0:
                return y0
            dy = y1 - y0
            return y0 + ((x - x0) * dy) // dx
    return pts[-1][1]


def curve_expected(speed: int, cadence: int) -> Tuple[int, int, int]:
    """Return (curve_power_w, cadence_q15, scaled_power_w) for trail profile."""
    speed_curve = [(0, 180), (60, 260), (120, 420), (180, 560), (220, 680), (280, 750)]
    cadence_curve = [(50, 24576), (80, 32768), (110, 29491), (140, 20480)]
    pw = interp_linear(speed, speed_curve)
    cq15 = interp_linear(cadence, cadence_curve)
    scaled = (pw * cq15 + (1 << 14)) >> 15
    if scaled < 0:
        scaled = 0
    if scaled > 0xFFFF:
        scaled = 0xFFFF
    return pw, cq15, scaled


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

        # Use trail profile (id=1) so cap_speed matches curve max (280 dmph).
        client.set_profile(1, persist=False)

        base_rpm = 200
        torque = 80
        throttle = 60  # big enough to exceed curve limit so clamp is visible
        soc = 90
        err = 0

        cases = [
            # (speed, cadence, expect_power, note)
            (0, 80, "low-end clamp"),
            (90, 80, "mid slope"),
            (200, 80, "upper slope"),
            (120, 140, "cadence taper"),
            (280, 80, "top end clamp"),
            (300, 80, "speed cap zeros"),
        ]

        for speed, cadence, note in cases:
            client.set_state(
                rpm=base_rpm,
                torque=torque,
                speed_dmph=speed,
                soc=soc,
                err=err,
                cadence_rpm=cadence,
                throttle_pct=throttle,
                brake=0,
                buttons=0,
            )
            dbg = client.debug_state()
            expect(dbg.version >= 3, "debug_state version should be >=3")
            base_power = (throttle * 8) + (torque // 4)
            curve_pw, cq15, scaled_pw = curve_expected(speed, cadence)
            expected_power = min(base_power, scaled_pw)

            if speed > 280:
                expect(dbg.assist_mode == 0, f"{note}: assist should be off past cap")
                expect(dbg.cmd_power_w == 0, f"{note}: power should zero past cap")
                continue

            expect(dbg.curve_power_w == scaled_pw, f"{note}: curve_power_w mismatch")
            expect(dbg.curve_cadence_q15 == cq15, f"{note}: cadence_q15 mismatch")
            expect(dbg.cmd_power_w == expected_power, f"{note}: cmd_power_w mismatch")
            expect(dbg.cmd_current_dA == expected_power // 2, f"{note}: current mismatch")
            expect(dbg.assist_mode == 1, f"{note}: assist_mode should be on")

        print("PASS: assist curve interpolation + caps")
        return 0
    except (ProtocolError, AssertionError) as e:
        sys.stderr.write(f"FAIL: {e}\n")
        return 1
    finally:
        client.close()


if __name__ == "__main__":
    raise SystemExit(main())
