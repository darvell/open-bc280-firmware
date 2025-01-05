#!/usr/bin/env python3
"""Renode-driven strip chart MVP test for BC280 open firmware.

Validates:
- Downsampling: graph samples follow the latest input seen in each period.
- Sample count/min/max/latest track the downsampled series.
- Graph tick cadence stays near the configured period.
- Window metadata (capacity * period) is consistent.
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

BASE_SPEED = 100
STEP_DELTA = 5
PERIODS = 8
STEPS_PER_PERIOD = 4
PING_EVERY = 6


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


def parse_trace_log(path: str) -> Tuple[List[Tuple[int, int]], List[Dict[str, int]]]:
    set_states: List[Tuple[int, int]] = []
    graphs: List[Dict[str, int]] = []
    if not os.path.exists(path):
        return set_states, graphs
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
            if label == "set_state":
                if "ms" in kv and "spd" in kv:
                    set_states.append((kv["ms"], kv["spd"]))
            elif label == "graph":
                want = {"ms", "cnt", "cap", "min", "max", "latest", "period", "window"}
                if want.issubset(kv.keys()):
                    graphs.append({k: kv[k] for k in want})
    return set_states, graphs


def main() -> int:
    try:
        wait_for_pty(PORT)
    except FileNotFoundError as e:
        sys.stderr.write(str(e) + "\n")
        return 1

    # Reset UART log so traces from this run are deterministic.
    try:
        if os.path.exists(UART_LOG):
            os.remove(UART_LOG)
    except Exception:
        pass

    client = UARTClient(PORT, baud=115200, timeout=0.5)
    try:
        client.ping()
        client.graph_select(GRAPH_CHANNELS["speed"], GRAPH_WINDOWS["30s"], reset=True)

        summary = client.graph_summary()
        period_ms = summary.period_ms
        expect(period_ms > 0, "graph period must be >0")
        step_ms = max(1, period_ms // STEPS_PER_PERIOD)
        total_steps = PERIODS * STEPS_PER_PERIOD

        for i in range(total_steps):
            speed = BASE_SPEED + i * STEP_DELTA
            client.set_state(rpm=0, torque=0, speed_dmph=speed, soc=0, err=0)
            if (i % PING_EVERY) == 0:
                client.ping()
            time.sleep(step_ms / 1000.0)

        # Allow graph ticks to flush into the trace log.
        time.sleep((period_ms / 1000.0) * 2.0)

        set_states, graphs = parse_trace_log(UART_LOG)
        expect(set_states, "no set_state traces found")
        expect(graphs, "no graph traces found (build the Renode test image)")

        # Ignore any graph ticks before the first set_state.
        first_ms = set_states[0][0]
        graphs = [g for g in graphs if g["ms"] >= first_ms]
        expect(graphs, "no graph ticks after first set_state")

        # Validate cadence.
        tol = max(5, period_ms // 8)
        for a, b in zip(graphs, graphs[1:]):
            delta = b["ms"] - a["ms"]
            expect(abs(delta - period_ms) <= tol, f"graph tick delta {delta}ms out of tolerance")

        # Map each graph tick to the latest input before it.
        idx = 0
        expected_samples: List[int] = []
        for g in graphs:
            while idx + 1 < len(set_states) and set_states[idx + 1][0] <= g["ms"]:
                idx += 1
            expected = set_states[idx][1]
            expected_samples.append(expected)
            expect(g["latest"] == expected, f"graph sample {g['latest']} != expected {expected}")

        last = graphs[-1]
        expected_min = min(expected_samples)
        expected_max = max(expected_samples)
        expected_cnt = len(expected_samples)

        if expected_cnt <= last["cap"]:
            expect(last["cnt"] == expected_cnt, f"count {last['cnt']} != {expected_cnt}")
        else:
            expect(last["cnt"] == last["cap"], "count should saturate at capacity")
        expect(last["min"] == expected_min, f"min {last['min']} != {expected_min}")
        expect(last["max"] == expected_max, f"max {last['max']} != {expected_max}")
        expect(last["latest"] == expected_samples[-1], "latest sample mismatch")
        expect(last["window"] == last["cap"] * last["period"], "window_ms mismatch")

        print("PASS: graph strip chart downsampling + window behavior")
        return 0
    except (ProtocolError, AssertionError) as e:
        sys.stderr.write(f"FAIL: {e}\n")
        return 1
    finally:
        client.close()


if __name__ == "__main__":
    raise SystemExit(main())
