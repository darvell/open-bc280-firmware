#!/usr/bin/env python3
"""Renode regression for stream log (sampled telemetry).

Assumptions:
- Renode exposes UART1 via BC280_UART1_PTY (e.g. /tmp/uart1).
- Firmware built from this repo (Meson/Ninja; see scripts/build_open_firmware.sh).

Test flow:
1) Wait for PTY, ping.
2) Enable stream logging at a known period.
3) Feed deterministic telemetry states, waiting for each sample.
4) Read back the samples in chunks and verify values/order.
"""

import os
import sys
import time
from typing import List, Tuple

from uart_client import ProtocolError, UARTClient

PORT = os.environ.get("BC280_UART1_PTY", "/tmp/uart1")
PERIOD_MS = 200

Sample = Tuple[int, int, int, int, int, int, int]
# speed_dmph, cadence_rpm, power_w, batt_dV, batt_dA, temp_dC, brake
SAMPLES: List[Sample] = [
    (110, 60, 120, 120, -15, 250, 0),
    (123, 70, 140, 118, -20, 255, 1),
    (145, 80, 160, 116, -25, 260, 0),
    (167, 90, 180, 114, -30, 265, 0),
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


def wait_for_ping(client: UARTClient, tries: int = 15, delay: float = 0.1) -> None:
    for _ in range(tries):
        try:
            client.ping()
            return
        except Exception:
            time.sleep(delay)
    raise ProtocolError("ping did not recover")


def wait_for_count(client: UARTClient, target: int, timeout_s: float = 2.0) -> None:
    deadline = time.time() + timeout_s
    while time.time() < deadline:
        s = client.stream_log_summary()
        if s.count >= target:
            return
        time.sleep(0.05)
    raise AssertionError(f"timeout waiting for stream log count {target}")


def main() -> int:
    try:
        wait_for_pty(PORT)
    except FileNotFoundError as e:
        sys.stderr.write(str(e) + "\n")
        return 1

    client = UARTClient(PORT, baud=115200, timeout=0.5)
    try:
        wait_for_ping(client)
        before = client.stream_log_summary()

        client.stream_log_control(enable=True, period_ms=PERIOD_MS)

        target = before.count
        for speed, cadence, power_w, batt_dv, batt_da, temp_dc, brake in SAMPLES:
            client.set_state(
                rpm=0,
                torque=0,
                speed_dmph=speed,
                soc=80,
                err=0,
                cadence_rpm=cadence,
                brake=brake,
                power_w=power_w,
                batt_dV=batt_dv,
                batt_dA=batt_da,
                ctrl_temp_dC=temp_dc,
            )
            target += 1
            wait_for_count(client, target)

        after = client.stream_log_summary()
        expect(after.count >= before.count + len(SAMPLES), "stream log did not capture expected samples")
        expect(after.record_size == 20, f"record size mismatch {after.record_size}")

        offset = before.count
        first_chunk = client.stream_log_read(offset=offset, limit=2)
        offset += len(first_chunk)
        second_chunk = client.stream_log_read(offset=offset, limit=4)
        records = first_chunk + second_chunk
        expect(len(records) >= len(SAMPLES), f"expected >= {len(SAMPLES)} records, got {len(records)}")

        for idx, sample in enumerate(SAMPLES):
            rec = records[idx]
            speed, cadence, power_w, batt_dv, batt_da, temp_dc, brake = sample
            expect(rec.version == 1, "record version mismatch")
            expect(rec.speed_dmph == speed, f"speed mismatch {rec.speed_dmph} != {speed}")
            expect(rec.cadence_rpm == cadence, f"cadence mismatch {rec.cadence_rpm} != {cadence}")
            expect(rec.power_w == power_w, f"power mismatch {rec.power_w} != {power_w}")
            expect(rec.batt_dV == batt_dv, f"batt_dV mismatch {rec.batt_dV} != {batt_dv}")
            expect(rec.batt_dA == batt_da, f"batt_dA mismatch {rec.batt_dA} != {batt_da}")
            expect(rec.temp_dC == temp_dc, f"temp mismatch {rec.temp_dC} != {temp_dc}")
            if brake:
                expect((rec.flags & 0x01) != 0, "brake flag not set")

        client.stream_log_control(enable=False)
        print("PASS: stream log capture/read/resume")
        return 0
    except (AssertionError, ProtocolError) as e:
        sys.stderr.write(f"FAIL: {e}\n")
        return 1
    finally:
        client.close()


if __name__ == "__main__":
    raise SystemExit(main())
