#!/usr/bin/env python3
"""Renode-driven bus capture regression for BC280 open firmware.

Assumptions:
- Renode was launched headless with BC280_UART1_PTY pointing at a PTY (e.g. /tmp/uart1).
- open-firmware build is loaded by the Renode script (`renode/bc280_open_firmware.resc`).

Test plan:
1) Disable capture + reset ring. Inject frame and ensure it is ignored.
2) Enable capture, inject deterministic frames with explicit dt.
3) Read back in chunks and assert order + timestamps.
"""

import os
import sys
import time
from typing import List, Tuple

from uart_client import ProtocolError, UARTClient

PORT = os.environ.get("BC280_UART1_PTY", "/tmp/uart1")


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


def main() -> int:
    try:
        wait_for_pty(PORT)
    except FileNotFoundError as e:
        sys.stderr.write(str(e) + "\n")
        return 1

    client = UARTClient(PORT, baud=115200, timeout=0.5)
    try:
        client.ping()

        # Disabled capture: reset ring and verify injections are ignored.
        client.bus_capture_control(enable=False, reset=True)
        summary = client.bus_capture_summary()
        expect(not summary.enabled, "capture should be disabled")
        expect(summary.count == 0, f"expected empty ring, got {summary.count}")

        status = client.bus_capture_inject(1, 5, b"\x01\x02")
        expect(status == 0xFD, f"expected disabled status 0xFD, got 0x{status:02x}")
        summary = client.bus_capture_summary()
        expect(summary.count == 0, "disabled capture should not record frames")

        # Enable capture and inject deterministic frames.
        client.bus_capture_control(enable=True, reset=True)
        frames: List[Tuple[int, int, bytes]] = [
            (1, 0, b"\x10\x20\x30"),
            (2, 5, b"\xAA"),
            (3, 12, b"\xBE\xEF"),
        ]
        for bus_id, dt_ms, payload in frames:
            status = client.bus_capture_inject(bus_id, dt_ms, payload)
            expect(status == 0, f"inject failed status=0x{status:02x}")

        summary = client.bus_capture_summary()
        expect(summary.enabled, "capture should be enabled")
        expect(summary.count == len(frames), f"count mismatch {summary.count} != {len(frames)}")

        # Read back in chunks to exercise offset + limit.
        chunk1 = client.bus_capture_read(offset=0, limit=1)
        expect(len(chunk1) == 1, f"expected 1 record, got {len(chunk1)}")
        expect(chunk1[0].dt_ms == frames[0][1], "dt mismatch for first record")
        expect(chunk1[0].bus_id == frames[0][0], "bus_id mismatch for first record")
        expect(chunk1[0].data == frames[0][2], "data mismatch for first record")

        chunk2 = client.bus_capture_read(offset=1, limit=2)
        expect(len(chunk2) == 2, f"expected 2 records, got {len(chunk2)}")
        for idx, rec in enumerate(chunk2, start=1):
            exp = frames[idx]
            expect(rec.dt_ms == exp[1], f"dt mismatch for record {idx}")
            expect(rec.bus_id == exp[0], f"bus_id mismatch for record {idx}")
            expect(rec.data == exp[2], f"data mismatch for record {idx}")

        print("PASS: bus capture injection + chunked readback")
        return 0
    except (ProtocolError, AssertionError) as e:
        sys.stderr.write(f"FAIL: {e}\n")
        return 1
    finally:
        client.close()


if __name__ == "__main__":
    raise SystemExit(main())
