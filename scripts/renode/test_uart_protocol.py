#!/usr/bin/env python3
"""Headless Renode UART protocol test using the built-in uart_client helper.

This script assumes Renode was started headless with BC280_UART1_PTY=/tmp/uart1
so that the USART1 stub bridges to a host PTY. It exercises:
- ping
- state set + dump roundtrip
- streaming at a requested period (+/- tolerance)

Usage:
  BC280_UART1_PTY=/tmp/uart1 ./scripts/renode/test_uart_protocol.py

Run via the Renode harness/CI: the caller should launch Renode separately
(e.g. through scripts/renode_smoke.sh or a dedicated test harness) and set
the PTY env.
"""

import os
import sys
import time

from uart_client import ProtocolError, UARTClient, TelemetryV1

PORT = os.environ.get("BC280_UART1_PTY", "/tmp/uart1")
OUTDIR = os.environ.get("BC280_RENODE_OUTDIR") or os.path.abspath(
    os.path.join(os.path.dirname(__file__), "..", "..", "out", "renode")
)
UART_LOG = os.path.join(OUTDIR, "uart1_tx.log")
STREAM_PERIOD_MS = 200
STREAM_TOLERANCE_MS = 80
STREAM_FRAMES = 3
TEST_BATT_DV = 512   # 51.2 V
TEST_BATT_DA = -35   # -3.5 A (regen/charge)
TEST_CTRL_TEMP_DC = 476  # 47.6 C


def expect(cond: bool, msg: str) -> None:
    if not cond:
        raise AssertionError(msg)


def main() -> int:
    # Wait briefly for the PTY to appear (Renode usart1 stub creates it on first use).
    for _ in range(50):
        if os.path.exists(PORT):
            break
        time.sleep(0.1)
    if not os.path.exists(PORT):
        sys.stderr.write(f"UART PTY not found at {PORT}\n")
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

        # Set bootloader flag (recovery path uses this without reboot).
        client.set_bootloader_flag()

        # Set a known state and verify roundtrip via dump.
        want = dict(rpm=220, torque=50, speed_dmph=123, soc=87, err=1)
        client.set_state(
            **want,
            cadence_rpm=88,
            power_w=321,
            batt_dV=TEST_BATT_DV,
            batt_dA=TEST_BATT_DA,
            ctrl_temp_dC=TEST_CTRL_TEMP_DC,
        )
        st = client.state_dump()
        for k, v in want.items():
            expect(getattr(st, k) == v, f"state field {k} mismatch {getattr(st, k)} != {v}")

        # Enable streaming and capture a few frames; check spacing.
        client.set_stream(STREAM_PERIOD_MS)
        timestamps = []
        for _ in range(STREAM_FRAMES):
            payload = client.read_stream_frame(timeout_ms=STREAM_PERIOD_MS + 100)
            s = TelemetryV1.from_payload(payload)
            timestamps.append(s.ms)
            expect(s.speed_dmph == want["speed_dmph"], "telemetry speed mismatch")
            expect(s.cadence_rpm == 88, "telemetry cadence mismatch")
            expect(s.power_w == 321, "telemetry power mismatch")
            expect(s.batt_dV == TEST_BATT_DV, "telemetry battery voltage mismatch")
            expect(s.batt_dA == TEST_BATT_DA, "telemetry battery current mismatch")
            expect(s.ctrl_temp_dC == TEST_CTRL_TEMP_DC, "telemetry temp mismatch")
            expect(s.profile_id == st.profile_id, "telemetry profile mismatch")
        # Disable stream.
        client.set_stream(0)

        # Ensure stream stops promptly: expect timeout when reading again.
        try:
            client.read_stream_frame(timeout_ms=STREAM_PERIOD_MS + 100)
            raise AssertionError("stream did not stop after disable")
        except UARTClient.ProtocolError:
            pass

        # Check inter-frame deltas.
        deltas = [b - a for a, b in zip(timestamps, timestamps[1:])]
        for d in deltas:
            expect(abs(d - STREAM_PERIOD_MS) <= STREAM_TOLERANCE_MS, f"stream delta {d}ms out of tolerance")

        # Give firmware a moment to emit trace/status lines.
        time.sleep(0.2)

        # Validate Renode trace output when present.
        if os.path.exists(UART_LOG):
            with open(UART_LOG, "r", errors="ignore") as f:
                lines = [ln.strip() for ln in f.readlines() if "[TRACE]" in ln]
            expect(lines, "no trace lines found in UART log")
            trace_set_state = [ln for ln in lines if ln.startswith("[TRACE] set_state")]
            expect(trace_set_state, "missing set_state trace")
            expect("spd=123" in trace_set_state[0], "trace missing speed")
            expect("rpm=220" in trace_set_state[0], "trace missing rpm")
            # Status traces should reflect streaming period after set_stream.
            trace_status = [ln for ln in lines if ln.startswith("[TRACE] status")]
            expect(trace_status, "missing status trace")
            expect(any("stream=200" in ln for ln in trace_status), "status trace missing stream=200")

        print("PASS: ping, state roundtrip, streaming timing, trace output")
        return 0
    except ProtocolError as e:
        sys.stderr.write(f"Protocol error: {e}\n")
        return 1
    except AssertionError as e:
        sys.stderr.write(f"FAIL: {e}\n")
        return 1
    finally:
        try:
            client.set_stream(0)
        except Exception:
            pass
        client.close()


if __name__ == "__main__":
    raise SystemExit(main())
