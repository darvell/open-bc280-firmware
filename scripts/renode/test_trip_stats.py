#!/usr/bin/env python3
"""Renode trip statistics regression.

Drives deterministic speed + power samples and validates trip accumulators:
- distance and elapsed/moving time
- energy (mWh) and Wh/mi, Wh/km conversions
- persistence of last-ride summary after trip_reset
"""

import os
import sys
import time
from typing import List

from uart_client import UARTClient, ProtocolError

PORT = os.environ.get("BC280_UART1_PTY", "/tmp/uart1")
SAMPLE_DT_MS = 200
SAMPLES: List[int] = [50, 80, 120, 80, 50, 0, 90, 90, 60, 40]  # deci-mph
POWER_W = 150  # constant power sample


def expect(cond: bool, msg: str) -> None:
    if not cond:
        raise AssertionError(msg)


def approx(a: float, b: float, tol_pct: float = 2.0) -> bool:
    if b == 0:
        return abs(a) < 1e-6
    return abs(a - b) <= (abs(b) * tol_pct / 100.0)


def expected_distance_mm(samples: List[int], dt_ms: int) -> float:
    # Firmware: dist = speed_dmph * dt * 44704 / 1e6 (mm)
    total = 0.0
    for s in samples:
        total += s * dt_ms * 44704 / 1_000_000.0
    return total


def expected_energy_mwh(power_w: int, total_ms: int) -> float:
    # Firmware: mWh = power * dt_ms / 3600
    return power_w * total_ms / 3600.0


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
        client.trip_reset()  # start clean

        # Drive samples at fixed cadence.
        for s in SAMPLES:
            client.set_state(rpm=0, torque=0, speed_dmph=s, soc=90, err=0, power_w=POWER_W)
            time.sleep(SAMPLE_DT_MS / 1000.0)

        trip = client.trip_get()
        total_ms = len(SAMPLES) * SAMPLE_DT_MS
        exp_dist = expected_distance_mm(SAMPLES, SAMPLE_DT_MS)
        exp_energy = expected_energy_mwh(POWER_W, total_ms)

        expect(trip.active.elapsed_ms >= total_ms - 10, "elapsed_ms too low")
        expect(trip.active.moving_ms >= total_ms - 10, "moving_ms too low")
        expect(approx(trip.active.distance_mm, exp_dist, 3.0), f"distance mismatch {trip.active.distance_mm} vs {exp_dist}")
        expect(approx(trip.active.energy_mwh, exp_energy, 3.5), f"energy mismatch {trip.active.energy_mwh} vs {exp_energy}")

        miles = trip.active.distance_mm / 1_609_340.0
        if miles > 0:
            wh_per_mile = (trip.active.energy_mwh / 1000.0) / miles
            expect(approx(trip.active.wh_per_mile_d10 / 10.0, wh_per_mile, 5.0), "Wh/mi mismatch")

        km = trip.active.distance_mm / 1_000_000.0
        if km > 0:
            wh_per_km = (trip.active.energy_mwh / 1000.0) / km
            expect(approx(trip.active.wh_per_km_d10 / 10.0, wh_per_km, 5.0), "Wh/km mismatch")

        # Persist into last summary and ensure active clears.
        client.trip_reset()
        trip2 = client.trip_get()
        expect(trip2.last_valid, "last summary not marked valid")
        expect(trip2.active.elapsed_ms == 0, "active not cleared after reset")
        expect(trip2.last.distance_mm == trip.active.distance_mm, "last distance mismatch")
        expect(trip2.last.energy_mwh == trip.active.energy_mwh, "last energy mismatch")

        print("PASS: trip stats distance/energy and persistence")
        return 0
    except (ProtocolError, AssertionError) as e:
        sys.stderr.write(f"FAIL: {e}\n")
        return 1
    finally:
        client.close()


if __name__ == "__main__":
    raise SystemExit(main())
