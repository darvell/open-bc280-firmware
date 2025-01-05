#!/usr/bin/env python3
"""Renode regression for timeline markers derived from the event log.

Validates:
- Marker positions are computed relative to the active graph window.
- Cruise on/off, brake, and thermal-derate markers are reported in order.
- Markers can be toggled off without affecting graph traces.
"""

import os
import sys
import time
from typing import Dict, List, Tuple

from uart_client import GRAPH_CHANNELS, GRAPH_WINDOWS, ProtocolError, UARTClient

PORT = os.environ.get("BC280_UART1_PTY", "/tmp/uart1")
OUTDIR = os.environ.get("BC280_RENODE_OUTDIR") or os.path.abspath(
    os.path.join(os.path.dirname(__file__), "..", "..", "out", "renode")
)
UART_LOG = os.path.join(OUTDIR, "uart1_tx.log")

# Event types + flags (from firmware)
EVT_BRAKE = 1
EVT_DERATE_ACTIVE = 5
EVT_CRUISE_EVENT = 6

CRUISE_EVT_ENGAGE_SPEED = 0x01
CRUISE_EVT_CANCEL_BRAKE = 0x11

LIMIT_REASON_THERM = 2

# Marker types (from firmware)
MARKER_CRUISE_ON = 1
MARKER_CRUISE_OFF = 2
MARKER_BRAKE = 3
MARKER_DERATE = 4


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


def reset_uart_log(path: str) -> None:
    try:
        if os.path.exists(path):
            os.remove(path)
    except Exception:
        pass


def marker_type_from_event(rtype: int, flags: int) -> int:
    if rtype == EVT_BRAKE:
        return MARKER_BRAKE
    if rtype == EVT_DERATE_ACTIVE:
        reason = flags & 0x0F
        if reason == LIMIT_REASON_THERM:
            return MARKER_DERATE
        return 0
    if rtype == EVT_CRUISE_EVENT:
        if flags == CRUISE_EVT_ENGAGE_SPEED:
            return MARKER_CRUISE_ON
        if flags == CRUISE_EVT_CANCEL_BRAKE:
            return MARKER_CRUISE_OFF
    return 0


def parse_trace_log(path: str) -> Tuple[List[Dict[str, int]], List[Dict[str, int]]]:
    markers: List[Dict[str, int]] = []
    graphs: List[Dict[str, int]] = []
    if not os.path.exists(path):
        return markers, graphs
    with open(path, "r", errors="ignore") as f:
        for raw in f:
            line = raw.strip()
            if not line.startswith("[TRACE]"):
                continue
            parts = line.split()
            if len(parts) < 3:
                continue
            label = parts[1]
            kv: Dict[str, int] = {}
            lists: Dict[str, List[int]] = {}
            for token in parts[2:]:
                if "=" not in token:
                    continue
                key, val = token.split("=", 1)
                if key in ("types", "pos"):
                    if not val:
                        lists[key] = []
                    else:
                        try:
                            lists[key] = [int(x) for x in val.split(",") if x]
                        except ValueError:
                            continue
                else:
                    try:
                        kv[key] = int(val)
                    except ValueError:
                        continue
            if label == "markers":
                if {"ms", "window", "count"}.issubset(kv.keys()):
                    kv["types"] = lists.get("types", [])
                    kv["pos"] = lists.get("pos", [])
                    markers.append(kv)
            elif label == "graph":
                if "ms" in kv:
                    graphs.append(kv)
    return markers, graphs


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
        client.graph_select(GRAPH_CHANNELS["speed"], GRAPH_WINDOWS["30s"], reset=True)
        client.marker_control(enable=True)

        summary = client.graph_summary()
        period_ms = summary.period_ms
        step_ms = max(20, period_ms // 4)

        before = client.event_log_summary()

        client.set_state(rpm=0, torque=0, speed_dmph=100, soc=0, err=0)
        client.log_event_mark(EVT_CRUISE_EVENT, CRUISE_EVT_ENGAGE_SPEED)
        time.sleep(step_ms / 1000.0)
        client.log_event_mark(EVT_CRUISE_EVENT, CRUISE_EVT_CANCEL_BRAKE)
        time.sleep(step_ms / 1000.0)
        client.log_event_mark(EVT_BRAKE, 0)
        time.sleep(step_ms / 1000.0)
        client.log_event_mark(EVT_DERATE_ACTIVE, LIMIT_REASON_THERM)

        time.sleep((period_ms / 1000.0) * 2.0)

        recs = client.event_log_read(offset=before.count, limit=8)
        expect(len(recs) >= 4, f"expected >=4 new events, got {len(recs)}")

        markers, graphs = parse_trace_log(UART_LOG)
        expect(graphs, "no graph traces found (build the Renode test image)")
        expect(markers, "no marker traces found (enable markers to test)")

        last = markers[-1]
        window_ms = last["window"]
        window_start = max(0, last["ms"] - window_ms)
        expected_types: List[int] = []
        expected_pos: List[int] = []
        for r in recs:
            if r.ms < window_start or r.ms > last["ms"]:
                continue
            mtype = marker_type_from_event(r.type, r.flags)
            if not mtype:
                continue
            expected_types.append(mtype)
            expected_pos.append(r.ms - window_start)

        expect(last["count"] == len(expected_types), "marker count mismatch")
        expect(last["types"] == expected_types, f"marker types mismatch: {last['types']} vs {expected_types}")
        expect(last["pos"] == expected_pos, f"marker positions mismatch: {last['pos']} vs {expected_pos}")

        # Disable markers and ensure they no longer emit.
        client.marker_control(enable=False)
        reset_uart_log(UART_LOG)
        time.sleep((period_ms / 1000.0) * 2.0)
        markers2, graphs2 = parse_trace_log(UART_LOG)
        expect(graphs2, "graph traces missing after marker disable")
        expect(not markers2, "markers should be absent when disabled")

        print("PASS: timeline markers on/off + window-relative positions")
        return 0
    except (AssertionError, ProtocolError) as e:
        sys.stderr.write(f"FAIL: {e}\n")
        return 1
    finally:
        client.close()


if __name__ == "__main__":
    raise SystemExit(main())
