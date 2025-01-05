#!/usr/bin/env python3
"""Renode histogram regression.

Validates:
- time-in-assist (profile buckets) and gear time histograms
- power distribution histogram (fixed bins)

Requires UART1 PTY exposed via BC280_UART1_PTY and Renode trace output in
`uart1_tx.log` (standard open-firmware image).
"""

import os
import sys
import time
from typing import Dict, List, Tuple

from uart_client import UARTClient, ProtocolError

PORT = os.environ.get("BC280_UART1_PTY", "/tmp/uart1")
OUTDIR = os.environ.get("BC280_RENODE_OUTDIR") or os.path.abspath(
    os.path.join(os.path.dirname(__file__), "..", "..", "out", "renode")
)
UART_LOG = os.path.join(OUTDIR, "uart1_tx.log")

SAMPLE_DT_MS = 200
TORQUE = 200
CADENCE = 80
SPEED_DMPH = 120

ASSIST_BINS = 5
GEAR_BINS = 12
POWER_BINS = 16
POWER_BIN_W = 100

STEPS: List[Dict[str, int]] = [
    {"profile": 0, "gear": 1, "power": 50, "buttons": 0x00},
    {"profile": 0, "gear": 1, "power": 120, "buttons": 0x00},
    {"profile": 1, "gear": 2, "power": 220, "buttons": 0x10},
    {"profile": 1, "gear": 2, "power": 80, "buttons": 0x00},
    {"profile": 2, "gear": 3, "power": 340, "buttons": 0x10},
    {"profile": 2, "gear": 3, "power": 510, "buttons": 0x00},
    {"profile": 1, "gear": 2, "power": 180, "buttons": 0x20},
    {"profile": 0, "gear": 1, "power": 420, "buttons": 0x20},
    {"profile": 0, "gear": 1, "power": 900, "buttons": 0x00},
]


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


def parse_set_state_times(path: str) -> List[int]:
    times: List[int] = []
    if not os.path.exists(path):
        return times
    with open(path, "r", errors="ignore") as f:
        for raw in f:
            line = raw.strip()
            if not line.startswith("[TRACE]"):
                continue
            parts = line.split()
            if len(parts) < 3 or parts[1] != "set_state":
                continue
            kv: Dict[str, int] = {}
            for token in parts[2:]:
                if "=" not in token:
                    continue
                key, val = token.split("=", 1)
                try:
                    kv[key] = int(val)
                except ValueError:
                    continue
            if "ms" in kv:
                times.append(kv["ms"])
    return times


def parse_hist_line(path: str) -> Dict[str, List[int]]:
    if not os.path.exists(path):
        return {}
    last = ""
    with open(path, "r", errors="ignore") as f:
        for raw in f:
            line = raw.strip()
            if line.startswith("[TRACE] hist "):
                last = line
    if not last:
        return {}
    parts = last.split()
    out: Dict[str, List[int]] = {}
    for token in parts[2:]:
        if "=" not in token:
            continue
        key, val = token.split("=", 1)
        if key in {"assist", "gear", "power"}:
            out[key] = [int(x) for x in val.split(",") if x]
    return out


def expected_hist(set_state_ms: List[int]) -> Dict[str, List[int]]:
    expect(len(set_state_ms) == len(STEPS), "set_state count mismatch")
    assist = [0] * ASSIST_BINS
    gear = [0] * GEAR_BINS
    power = [0] * POWER_BINS

    gear_state = 1
    last_buttons = 0

    for i in range(1, len(set_state_ms)):
        step = STEPS[i]
        dt = set_state_ms[i] - set_state_ms[i - 1]
        if dt < 0:
            dt = 0

        buttons = step["buttons"]
        rising = buttons & (~last_buttons & 0xFF)
        if rising & 0x10:
            if gear_state < 12:
                gear_state += 1
        if rising & 0x20:
            if gear_state > 1:
                gear_state -= 1
        last_buttons = buttons

        profile = step["profile"]
        if profile < ASSIST_BINS:
            assist[profile] += dt
        if 1 <= gear_state <= GEAR_BINS:
            gear[gear_state - 1] += dt
        bin_idx = step["power"] // POWER_BIN_W
        if bin_idx >= POWER_BINS:
            bin_idx = POWER_BINS - 1
        power[bin_idx] += dt

    return {"assist": assist, "gear": gear, "power": power}


def assert_hist(actual: List[int], expected: List[int], label: str, tol_ms: int = 15) -> None:
    expect(len(actual) == len(expected), f"{label} bin length mismatch")
    for i, (a, e) in enumerate(zip(actual, expected)):
        if abs(a - e) > tol_ms:
            raise AssertionError(f"{label}[{i}] {a} != {e} (tol {tol_ms}ms)")


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
        client.trip_reset()
        client.set_gears(count=3, shape=0, min_q15=32768, max_q15=32768)

        current_profile = -1
        for step in STEPS:
            if step["profile"] != current_profile:
                client.set_profile(step["profile"], persist=False)
                current_profile = step["profile"]
            client.set_state(
                rpm=0,
                torque=TORQUE,
                speed_dmph=SPEED_DMPH,
                soc=90,
                err=0,
                cadence_rpm=CADENCE,
                buttons=step["buttons"],
                power_w=step["power"],
            )
            time.sleep(SAMPLE_DT_MS / 1000.0)

        client.histogram_trace()
        time.sleep(0.2)

        set_state_ms = parse_set_state_times(UART_LOG)
        expect(set_state_ms, "no set_state traces found (build the Renode test image)")
        hist = parse_hist_line(UART_LOG)
        expect(hist, "no hist trace found")

        expected = expected_hist(set_state_ms)
        assert_hist(hist["assist"], expected["assist"], "assist")
        assert_hist(hist["gear"], expected["gear"], "gear")
        assert_hist(hist["power"], expected["power"], "power")

        print("PASS: histogram assist/gear/power bins")
        return 0
    except (ProtocolError, AssertionError) as e:
        sys.stderr.write(f"FAIL: {e}\n")
        return 1
    finally:
        client.close()


if __name__ == "__main__":
    raise SystemExit(main())
