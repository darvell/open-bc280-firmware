#!/usr/bin/env python3
"""Renode-driven trip summary UI render-hash test for BC280 open firmware."""

import os
import sys
import time
from typing import Dict, List, Optional, Tuple

from uart_client import ProtocolError, UARTClient

PORT = os.environ.get("BC280_UART1_PTY", "/tmp/uart1")
OUTDIR = os.environ.get("BC280_RENODE_OUTDIR") or os.path.abspath(
    os.path.join(os.path.dirname(__file__), "..", "..", "open-firmware", "renode", "output")
)
UART_LOG = os.path.join(OUTDIR, "uart1_tx.log")

BTN_NEXT = 0x08

SPEED_DMPH = 120
POWER_W = 260


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


def parse_ui_nav(path: str) -> List[Dict[str, str]]:
    entries: List[Dict[str, str]] = []
    if not os.path.exists(path):
        return entries
    with open(path, "r", errors="ignore") as f:
        for raw in f:
            line = raw.strip()
            if not line.startswith("[TRACE] ui-nav"):
                continue
            parts = line.split()
            kv: Dict[str, str] = {}
            for token in parts[2:]:
                if "=" not in token:
                    continue
                key, val = token.split("=", 1)
                kv[key] = val
            if "ms" in kv and "name" in kv:
                entries.append(kv)
    return entries


def wait_for_nav(path: str, name: str, timeout_s: float = 3.0) -> Optional[int]:
    deadline = time.time() + timeout_s
    while time.time() < deadline:
        for entry in reversed(parse_ui_nav(path)):
            if entry.get("name") == name:
                try:
                    return int(entry.get("ms", "0"))
                except ValueError:
                    return None
        time.sleep(0.05)
    return None


def parse_ui_traces(path: str, min_ms: int) -> List[Dict[str, int]]:
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
            if "ms" in kv and kv["ms"] >= min_ms and "hash" in kv:
                entries.append(kv)
    return entries


def wait_for_traces(path: str, min_ms: int, count: int, timeout_s: float = 3.0) -> List[Dict[str, int]]:
    deadline = time.time() + timeout_s
    entries: List[Dict[str, int]] = []
    while time.time() < deadline:
        entries = parse_ui_traces(path, min_ms)
        if len(entries) >= count:
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
        client.trip_reset()

        # Script a deterministic trip segment.
        for _ in range(10):
            client.set_state(
                rpm=0,
                torque=0,
                speed_dmph=SPEED_DMPH,
                soc=90,
                err=0,
                cadence_rpm=70,
                buttons=0,
                power_w=POWER_W,
            )
            time.sleep(0.2)

        # Freeze stats.
        client.set_state(
            rpm=0,
            torque=0,
            speed_dmph=0,
            soc=90,
            err=0,
            cadence_rpm=0,
            buttons=0,
            power_w=0,
        )
        time.sleep(0.4)

        trip = client.trip_get()
        expect(trip.active.distance_mm > 0, "trip distance did not accumulate")
        expect(trip.active.moving_ms > 0, "trip moving time did not accumulate")

        # Navigate to trip screen (dashboard -> focus -> graphs -> trip).
        for _ in range(3):
            client.set_state(
                rpm=0,
                torque=0,
                speed_dmph=0,
                soc=90,
                err=0,
                cadence_rpm=0,
                buttons=BTN_NEXT,
                power_w=0,
            )
            time.sleep(0.1)
            client.set_state(
                rpm=0,
                torque=0,
                speed_dmph=0,
                soc=90,
                err=0,
                cadence_rpm=0,
                buttons=0,
                power_w=0,
            )
            time.sleep(0.1)

        nav_ms = wait_for_nav(UART_LOG, "trip", timeout_s=2.0)
        expect(nav_ms is not None, "ui-nav did not reach trip page")

        traces = wait_for_traces(UART_LOG, nav_ms or 0, count=2, timeout_s=2.0)
        expect(len(traces) >= 2, "insufficient ui traces after trip nav")

        last = traces[-1]
        prev = traces[-2]
        expect(last["hash"] == prev["hash"], "trip UI hash unstable across identical state")

        print("PASS: trip summary UI render-hash")
        return 0
    except (ProtocolError, AssertionError) as e:
        sys.stderr.write(f"FAIL: {e}\n")
        return 1
    finally:
        client.close()


if __name__ == "__main__":
    raise SystemExit(main())
