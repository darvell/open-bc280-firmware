#!/usr/bin/env python3
"""Renode-driven ring buffer regression for BC280 open firmware.

Assumptions:
- Renode was launched headless with BC280_UART1_PTY pointing at a PTY (e.g. /tmp/uart1).
- open-firmware build is loaded by the Renode script (`renode/bc280_open_firmware.resc`).

Test plan:
1) Wait for PTY, ping device.
2) Push a short sequence of speed samples via set_state(). Verify summary count/min/max/latest.
3) Push more than the ring capacity to exercise eviction and min/max maintenance.

The ring buffer is driven by set_state() speed_dmph; other fields are set to zero.
"""

import os
import sys
import time
from typing import List

from uart_client import ProtocolError, UARTClient

PORT = os.environ.get("BC280_UART1_PTY", "/tmp/uart1")
CAPACITY = 64  # matches g_speed_storage length in firmware

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


def push_speed_series(client: UARTClient, samples: List[int]) -> None:
    for s in samples:
        client.set_state(rpm=0, torque=0, speed_dmph=s, soc=0, err=0)


def main() -> int:
    try:
        wait_for_pty(PORT)
    except FileNotFoundError as e:
        sys.stderr.write(str(e) + "\n")
        return 1

    client = UARTClient(PORT, baud=115200, timeout=0.5)
    try:
        client.ping()

        # Phase 1: small sequence.
        seq1 = [120, 340, 90, 220]  # deci-mph
        push_speed_series(client, seq1)
        summary = client.ring_summary()
        expect(summary.count == len(seq1), f"count mismatch {summary.count} != {len(seq1)}")
        expect(summary.min == min(seq1), f"min mismatch {summary.min}")
        expect(summary.max == max(seq1), f"max mismatch {summary.max}")
        expect(summary.latest == seq1[-1], f"latest mismatch {summary.latest}")

        # Phase 2: overflow capacity to exercise eviction + min/max windows.
        seq2 = list(range(0, CAPACITY + 5))  # increasing 0..68
        push_speed_series(client, seq2)
        summary = client.ring_summary()
        expect(summary.count == CAPACITY, f"count after overflow {summary.count} != {CAPACITY}")
        # After pushing an increasing sequence longer than capacity, window holds last CAPACITY values.
        window = seq2[-CAPACITY:]
        expect(summary.min == min(window), f"min after overflow {summary.min}")
        expect(summary.max == max(window), f"max after overflow {summary.max}")
        expect(summary.latest == window[-1], f"latest after overflow {summary.latest}")

        print("PASS: ring buffer summary min/max/window behavior")
        return 0
    except (ProtocolError, AssertionError) as e:
        sys.stderr.write(f"FAIL: {e}\n")
        return 1
    finally:
        client.close()


if __name__ == "__main__":
    raise SystemExit(main())
