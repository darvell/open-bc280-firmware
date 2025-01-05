#!/usr/bin/env python3
"""Renode-driven multi-channel strip chart tests for BC280 open firmware.

Validates:
- Downsampling per channel: graph samples follow latest input seen in each period.
- Window min/max/count for 30s/2m/10m windows.
- Channel/window selection and reset behavior.
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

CHANNEL_TRACE_KEY = {
    "speed": "spd",
    "power": "pwr",
    "volt": "bv",
    "cadence": "cad",
    "temp": "tmp",
}

CHANNEL_WAVE = {
    "speed": (100, 7),
    "power": (200, 11),
    "volt": (420, 3),
    "cadence": (60, 4),
    "temp": (250, 2),
}

WINDOW_TARGET_MS = {
    "30s": 30000,
    "2m": 120000,
    "10m": 600000,
}

PERIODS_PER_WINDOW = {
    "30s": 6,
    "2m": 3,
    "10m": 2,
}

STEPS_PER_PERIOD = 4
PING_EVERY = 7


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


def read_log_from(path: str, offset: int) -> Tuple[List[str], int]:
    if not os.path.exists(path):
        return [], offset
    with open(path, "r", errors="ignore") as f:
        f.seek(offset)
        data = f.read()
        new_offset = f.tell()
    lines = [line.strip() for line in data.splitlines() if line.strip()]
    return lines, new_offset


def parse_trace_lines(lines: List[str]) -> Tuple[List[Dict[str, int]], List[Dict[str, int]]]:
    set_states: List[Dict[str, int]] = []
    graphs: List[Dict[str, int]] = []
    for raw in lines:
        if not raw.startswith("[TRACE]"):
            continue
        parts = raw.split()
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
        if label == "set_state" and "ms" in kv:
            set_states.append(kv)
        elif label == "graph":
            want = {"ms", "ch", "win", "cnt", "cap", "min", "max", "latest", "period", "window"}
            if want.issubset(kv.keys()):
                graphs.append({k: kv[k] for k in want})
    return set_states, graphs


def build_state_args(channel: str, value: int) -> Dict[str, int]:
    speed = 0
    cadence = 0
    power = 0
    batt_dv = 0
    temp = 0
    if channel == "speed":
        speed = value
    elif channel == "power":
        power = value
    elif channel == "volt":
        batt_dv = value
    elif channel == "cadence":
        cadence = value
    elif channel == "temp":
        temp = value
    return {
        "rpm": 0,
        "torque": 0,
        "speed_dmph": speed,
        "soc": 0,
        "err": 0,
        "cadence_rpm": cadence,
        "power_w": power,
        "batt_dV": batt_dv,
        "batt_dA": 0,
        "ctrl_temp_dC": temp,
    }


def run_case(client: UARTClient, log_offset: int, channel: str, window: str) -> int:
    channel_id = GRAPH_CHANNELS[channel]
    window_id = GRAPH_WINDOWS[window]
    client.graph_select(channel_id, window_id, reset=True)
    summary = client.graph_summary()
    period_ms = summary.period_ms
    expect(period_ms > 0, "graph period must be >0")

    target_ms = WINDOW_TARGET_MS[window]
    tol_ms = max(period_ms, period_ms // 2)
    expect(
        abs(summary.window_ms - target_ms) <= tol_ms,
        f"window {summary.window_ms}ms not close to {target_ms}ms",
    )

    step_ms = max(1, period_ms // STEPS_PER_PERIOD)
    total_steps = PERIODS_PER_WINDOW[window] * STEPS_PER_PERIOD
    base, delta = CHANNEL_WAVE[channel]

    for i in range(total_steps):
        value = base + i * delta
        client.set_state(**build_state_args(channel, value))
        if (i % PING_EVERY) == 0:
            client.ping()
        time.sleep(step_ms / 1000.0)

    time.sleep((period_ms / 1000.0) * 1.6)

    lines, new_offset = read_log_from(UART_LOG, log_offset)
    set_states, graphs = parse_trace_lines(lines)
    expect(set_states, f"no set_state traces for {channel}/{window} (build the Renode test image)")

    graphs = [g for g in graphs if g["ch"] == channel_id and g["win"] == window_id]
    expect(graphs, f"no graph traces for {channel}/{window}")

    first_ms = set_states[0]["ms"]
    graphs = [g for g in graphs if g["ms"] >= first_ms]
    expect(graphs, f"no graph ticks after first set_state for {channel}/{window}")

    key = CHANNEL_TRACE_KEY[channel]
    set_states = [s for s in set_states if key in s]
    expect(set_states, f"missing {key} in set_state traces")

    idx = 0
    expected_samples: List[int] = []
    for g in graphs:
        while idx + 1 < len(set_states) and set_states[idx + 1]["ms"] <= g["ms"]:
            idx += 1
        expected = set_states[idx][key]
        expected_samples.append(expected)
        expect(
            g["latest"] == expected,
            f"{channel}/{window} sample {g['latest']} != expected {expected}",
        )

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

    return new_offset


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

    client = UARTClient(PORT, baud=115200, timeout=0.6)
    offset = 0
    try:
        client.ping()

        for channel in GRAPH_CHANNELS.keys():
            for window in GRAPH_WINDOWS.keys():
                offset = run_case(client, offset, channel, window)

        print("PASS: multi-channel strip chart windows")
        return 0
    except (ProtocolError, AssertionError) as e:
        sys.stderr.write(f"FAIL: {e}\n")
        return 1
    finally:
        client.close()


if __name__ == "__main__":
    raise SystemExit(main())
