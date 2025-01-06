#!/usr/bin/env python3
"""Renode-driven dashboard UI render-hash test for BC280 open firmware.

Validates:
- Deterministic render hash for identical telemetry states.
- Trace fields reflect expected telemetry values.
- UI tick cadence stays near 200ms and render dt <= 200ms.
"""

import os
import sys
import time
from typing import Dict, List

from uart_client import ProtocolError, UARTClient

PORT = os.environ.get("BC280_UART1_PTY", "/tmp/uart1")
OUTDIR = os.environ.get("BC280_RENODE_OUTDIR") or os.path.abspath(
    os.path.join(os.path.dirname(__file__), "..", "..", "open-firmware", "renode", "output")
)
UART_LOG = os.path.join(OUTDIR, "uart1_tx.log")


EXPECTED = {
    "spd": 123,
    "soc": 87,
    "cad": 75,
    "pwr": 360,
    "bv": 520,
    "bi": 120,
}


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
            if not line.startswith("[TRACE] ui"):
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
            if "hash" in kv and "spd" in kv:
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
        client.set_state(
            rpm=220,
            torque=120,
            speed_dmph=EXPECTED["spd"],
            soc=EXPECTED["soc"],
            err=0,
            cadence_rpm=EXPECTED["cad"],
            throttle_pct=40,
            brake=0,
            buttons=0,
            power_w=EXPECTED["pwr"],
            batt_dV=EXPECTED["bv"],
            batt_dA=EXPECTED["bi"],
            ctrl_temp_dC=250,
        )

        # Wait for at least two UI ticks at 200ms cadence.
        time.sleep(0.6)
        client.ping()

        entries = wait_for_traces(UART_LOG, min_entries=2, timeout_s=2.0)
        expect(entries, "no ui traces found (RENODE_TEST build required)")

        # Keep only entries matching expected speed.
        filtered = [e for e in entries if e.get("spd") == EXPECTED["spd"]]
        expect(len(filtered) >= 2, "insufficient ui traces for expected state")

        last = filtered[-1]
        prev = filtered[-2]
        expect(last["hash"] == prev["hash"], "render hash unstable across identical state")

        for key, val in EXPECTED.items():
            expect(last.get(key) == val, f"{key}={last.get(key)} != expected {val}")

        expect(last.get("dt", 0) <= 200, f"render dt {last.get('dt', 0)}ms exceeds 200ms budget")

        # Validate tick cadence ~200ms between the last two UI ticks.
        delta = last.get("ms", 0) - prev.get("ms", 0)
        expect(160 <= delta <= 260, f"ui tick delta {delta}ms out of tolerance")

        print("PASS: dashboard UI render-hash + trace fields")
        return 0
    except (ProtocolError, AssertionError) as e:
        sys.stderr.write(f"FAIL: {e}\n")
        return 1
    finally:
        client.close()


if __name__ == "__main__":
    raise SystemExit(main())
