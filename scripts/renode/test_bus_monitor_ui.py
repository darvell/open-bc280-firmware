#!/usr/bin/env python3
"""Renode-driven bus monitor UI trace test for BC280 open firmware.

Validates:
- Filtering by bus_id + opcode (only matching frames emit trace lines).
- Diff mode highlights changed bytes between successive frames.
- Changed-only view outputs only the changed bytes.
- Ping remains responsive while bus monitor is active.
"""

import os
import sys
import time
from typing import Dict, List

from uart_client import ProtocolError, UARTClient

PORT = os.environ.get("BC280_UART1_PTY", "/tmp/uart1")
OUTDIR = os.environ.get("BC280_RENODE_OUTDIR") or os.path.abspath(
    os.path.join(os.path.dirname(__file__), "..", "..", "out", "renode")
)
UART_LOG = os.path.join(OUTDIR, "uart1_tx.log")

FLAG_ENABLE = 0x01
FLAG_FILTER_ID = 0x02
FLAG_FILTER_OPCODE = 0x04
FLAG_DIFF = 0x08
FLAG_CHANGED_ONLY = 0x10
FLAG_RESET = 0x20


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


def parse_trace_log(path: str) -> List[Dict[str, object]]:
    entries: List[Dict[str, object]] = []
    if not os.path.exists(path):
        return entries
    with open(path, "r", errors="ignore") as f:
        for raw in f:
            line = raw.strip()
            if not line.startswith("[TRACE] busui"):
                continue
            parts = line.split()
            if len(parts) < 3:
                continue
            kv: Dict[str, str] = {}
            for token in parts[2:]:
                if "=" not in token:
                    continue
                key, val = token.split("=", 1)
                kv[key] = val
            if "id" not in kv or "op" not in kv:
                continue
            try:
                entry = {
                    "id": int(kv.get("id", "0")),
                    "op": int(kv.get("op", "0")),
                    "len": int(kv.get("len", "0")),
                    "diff": int(kv.get("diff", "0"), 16) if kv.get("diff") else 0,
                    "data": kv.get("data", ""),
                }
            except ValueError:
                continue
            entries.append(entry)
    return entries


def wait_for_traces(path: str, min_entries: int, timeout_s: float = 2.0) -> List[Dict[str, object]]:
    deadline = time.time() + timeout_s
    entries: List[Dict[str, object]] = []
    while time.time() < deadline:
        entries = parse_trace_log(path)
        if len(entries) >= min_entries:
            return entries
        time.sleep(0.05)
    return entries


def parse_changed_only(data: str) -> Dict[int, int]:
    out: Dict[int, int] = {}
    if not data:
        return out
    for chunk in data.split(","):
        if ":" not in chunk:
            continue
        idx_s, val_s = chunk.split(":", 1)
        try:
            idx = int(idx_s)
            val = int(val_s, 16)
        except ValueError:
            continue
        out[idx] = val
    return out


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
        client.bus_capture_control(enable=True, reset=True)

        flags = FLAG_ENABLE | FLAG_FILTER_ID | FLAG_FILTER_OPCODE | FLAG_DIFF | FLAG_CHANGED_ONLY | FLAG_RESET
        client.bus_monitor_control(flags, bus_id=1, opcode=0x10)

        frames = [
            (1, 0, b"\x10\x01\x02"),
            (2, 1, b"\x10\xAA"),
            (1, 2, b"\x20\xFF"),
            (1, 3, b"\x10\x01\x03"),
        ]
        for bus_id, dt_ms, payload in frames:
            status = client.bus_capture_inject(bus_id, dt_ms, payload)
            expect(status == 0, f"inject failed status=0x{status:02x}")

        # Ensure responsiveness while monitor active.
        client.ping()

        entries = wait_for_traces(UART_LOG, min_entries=2, timeout_s=2.0)
        expect(entries, "no busui traces found (build the Renode test image)")

        for entry in entries:
            expect(entry["id"] == 1 and entry["op"] == 0x10, "non-matching frame leaked into busui traces")

        expect(len(entries) == 2, f"expected 2 matching entries, got {len(entries)}")

        first = entries[0]
        second = entries[1]

        first_changed = parse_changed_only(str(first["data"]))
        second_changed = parse_changed_only(str(second["data"]))

        expect(first_changed == {0: 0x10, 1: 0x01, 2: 0x02}, f"first changed-only data mismatch: {first_changed}")
        expect(second_changed == {2: 0x03}, f"second changed-only data mismatch: {second_changed}")
        expect(second["diff"] == 0x00000004, f"diff mask mismatch: 0x{second['diff']:08x}")

        print("PASS: bus monitor UI filter + diff + changed-only trace")
        return 0
    except (ProtocolError, AssertionError) as e:
        sys.stderr.write(f"FAIL: {e}\n")
        return 1
    finally:
        client.close()


if __name__ == "__main__":
    raise SystemExit(main())
