#!/usr/bin/env python3
"""Renode regression for bus injection/replay safety gating."""

import os
import sys
import time

from uart_client import ProtocolError, UARTClient, ConfigBlob

PORT = os.environ.get("BC280_UART1_PTY", "/tmp/uart1")
OUTDIR = os.environ.get("BC280_RENODE_OUTDIR") or os.path.abspath(
    os.path.join(os.path.dirname(__file__), "..", "..", "open-firmware", "renode", "output")
)
UART_LOG = os.path.join(OUTDIR, "uart1_tx.log")

# Event + inject flag bits (keep in sync with firmware)
EVT_BUS_INJECT = 10
FLAG_OK = 0x01
FLAG_BLOCKED_MOVING = 0x08
FLAG_REPLAY = 0x80

STATUS_MOVING = 0xF3


def wait_for_pty(path: str, timeout: float = 5.0) -> None:
    deadline = time.time() + timeout
    while time.time() < deadline:
        if os.path.exists(path):
            return
        time.sleep(0.1)
    raise FileNotFoundError(f"UART PTY not found at {path}")


def expect(cond: bool, msg: str) -> None:
    if not cond:
        raise AssertionError(msg)


def set_private_mode(client: UARTClient) -> None:
    cfg = client.config_get()
    cfg.mode = 1
    cfg.seq += 1
    client.config_stage(cfg)
    client.config_commit(reboot=False)


def find_event(recs, etype: int, flag_mask: int) -> bool:
    return any(r.type == etype and (r.flags & flag_mask) for r in recs)


def main() -> int:
    try:
        wait_for_pty(PORT)
    except FileNotFoundError as e:
        sys.stderr.write(str(e) + "\n")
        return 1

    # Reset UART log for deterministic trace checks.
    try:
        if os.path.exists(UART_LOG):
            os.remove(UART_LOG)
    except Exception:
        pass

    client = UARTClient(PORT, baud=115200, timeout=0.5)
    try:
        client.ping()
        set_private_mode(client)
        client.bus_capture_control(enable=True, reset=True)
        client.bus_inject_arm(armed=True, override=False)

        ev_before = client.event_log_summary()
        cap_before = client.bus_capture_summary()

        # Attempt injection while moving -> blocked + event logged.
        client.set_state(0, 0, 50, 90, 0, cadence_rpm=0, throttle_pct=0, brake=0, buttons=0)
        status = client.bus_capture_inject(bus_id=2, dt_ms=10, data=b"\x3a\x1a\x52")
        expect(status == STATUS_MOVING, f"expected moving block status, got 0x{status:02x}")

        ev_after = client.event_log_summary()
        new_recs = client.event_log_read(offset=ev_before.count, limit=8)
        expect(ev_after.count >= ev_before.count, "event log did not advance")
        expect(find_event(new_recs, EVT_BUS_INJECT, FLAG_BLOCKED_MOVING), "missing blocked-moving inject event")

        # Successful injection while stationary + brake.
        client.set_state(0, 0, 0, 90, 0, cadence_rpm=0, throttle_pct=0, brake=1, buttons=0)
        status_ok = client.bus_capture_inject(bus_id=2, dt_ms=5, data=b"\x01\x02\x03")
        expect(status_ok == 0, f"inject status expected 0, got 0x{status_ok:02x}")

        cap_after = client.bus_capture_summary()
        expect(cap_after.count == cap_before.count + 1, "capture count did not increment")
        records = client.bus_capture_read(offset=cap_after.count - 1, limit=1)
        expect(records, "no capture record returned")
        expect(records[0].bus_id == 2, "bus_id mismatch in capture record")
        expect(records[0].data == b"\x01\x02\x03", "capture data mismatch")

        # Trace line for inject should appear in UART log.
        time.sleep(0.2)
        if os.path.exists(UART_LOG):
            with open(UART_LOG, "r", errors="ignore") as f:
                lines = [ln.strip() for ln in f if "[TRACE] bus_inject" in ln]
            expect(lines, "missing bus_inject trace line")

        # Replay at bounded rate, then cancel via brake edge.
        status_replay = client.bus_capture_replay(offset=0, rate_ms=50)
        expect(status_replay == 0, f"replay start failed 0x{status_replay:02x}")
        time.sleep(0.25)
        cap_mid = client.bus_capture_summary()
        expect(cap_mid.count > cap_after.count, "replay did not append frames")

        # Trigger brake edge to cancel replay.
        client.set_state(0, 0, 0, 90, 0, cadence_rpm=0, throttle_pct=0, brake=0, buttons=0)
        client.set_state(0, 0, 0, 90, 0, cadence_rpm=0, throttle_pct=0, brake=1, buttons=0)
        time.sleep(0.15)
        cap_final = client.bus_capture_summary()
        time.sleep(0.15)
        cap_final2 = client.bus_capture_summary()
        expect(cap_final2.count == cap_final.count, "replay did not cancel on brake edge")

        # Ensure replay event logged.
        ev_post = client.event_log_summary()
        new_recs2 = client.event_log_read(offset=ev_after.count, limit=8)
        expect(find_event(new_recs2, EVT_BUS_INJECT, FLAG_REPLAY), "missing replay event")

        print("PASS: bus inject gating + replay cancel")
        return 0
    except (ProtocolError, AssertionError) as e:
        sys.stderr.write(f"FAIL: {e}\n")
        return 1
    finally:
        client.close()


if __name__ == "__main__":
    raise SystemExit(main())
