#!/usr/bin/env python3
"""Renode range estimate regression.

Simulates stable and highly variable Wh/mi to validate:
- range estimate converges to expected value
- confidence decreases under high variability

Usage:
  BC280_UART1_PTY=/tmp/uart1 ./scripts/renode/test_range_estimate.py
"""

import os
import sys
import time

from uart_client import ProtocolError, UARTClient

PORT = os.environ.get("BC280_UART1_PTY", "/tmp/uart1")
SAMPLE_DT_MS = 100


def expect(cond: bool, msg: str) -> None:
    if not cond:
        raise AssertionError(msg)


def approx(a: float, b: float, tol_pct: float = 10.0) -> bool:
    if b == 0:
        return abs(a) < 1e-6
    return abs(a - b) <= (abs(b) * tol_pct / 100.0)


def drive_samples(client: UARTClient, speed_dmph: int, power_w: int, soc: int, count: int) -> None:
    for _ in range(count):
        client.set_state(
            rpm=0,
            torque=0,
            speed_dmph=speed_dmph,
            soc=soc,
            err=0,
            power_w=power_w,
            batt_dV=480,
            batt_dA=0,
        )
        time.sleep(SAMPLE_DT_MS / 1000.0)


def main() -> int:
    try:
        deadline = time.time() + 5
        while time.time() < deadline:
            if os.path.exists(PORT):
                break
            time.sleep(0.05)
        else:
            raise FileNotFoundError(f"UART PTY not found at {PORT}")

        client = UARTClient(PORT, baud=115200, timeout=0.5)
    except Exception as e:
        sys.stderr.write(str(e) + "\n")
        return 1

    try:
        client.ping()

        # Stable Wh/mi: 200 W at 20 mph -> 10 Wh/mi (wh_per_mile_d10 = 100).
        speed = 200
        power = 200
        soc = 80
        drive_samples(client, speed, power, soc, 40)

        st = client.debug_state()
        expect(st.range_wh_per_mile_d10 > 0, "range Wh/mi not populated")
        expect(approx(st.range_wh_per_mile_d10 / 10.0, 10.0, 5.0), "Wh/mi average mismatch")

        # Nominal battery 500 Wh * 80% = 400 Wh => 40 mi (range_d10=400).
        expect(approx(st.range_est_d10, 400.0, 12.0), f"range estimate off: {st.range_est_d10}")
        expect(st.range_confidence >= 70, f"confidence too low: {st.range_confidence}")
        stable_conf = st.range_confidence

        # Highly variable load: alternate 100 W / 500 W to induce variance.
        for i in range(40):
            pwr = 100 if (i % 2 == 0) else 500
            client.set_state(
                rpm=0,
                torque=0,
                speed_dmph=speed,
                soc=soc,
                err=0,
                power_w=pwr,
                batt_dV=480,
                batt_dA=0,
            )
            time.sleep(SAMPLE_DT_MS / 1000.0)

        st2 = client.debug_state()
        expect(st2.range_confidence <= stable_conf - 15, "confidence did not decrease under variability")
        expect(st2.range_est_d10 > 0, "range estimate dropped to zero under variability")

        print("PASS: range estimate + confidence under stable/variable load")
        return 0
    except (AssertionError, ProtocolError) as e:
        sys.stderr.write(f"FAIL: {e}\n")
        return 1
    finally:
        client.close()


if __name__ == "__main__":
    raise SystemExit(main())
