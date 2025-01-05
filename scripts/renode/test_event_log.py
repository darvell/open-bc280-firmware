#!/usr/bin/env python3
"""Renode regression for event log ring buffer.

Assumptions:
- Renode exposes UART1 via BC280_UART1_PTY (e.g. /tmp/uart1).
- Firmware built from the standard open-firmware image (no special build flags).

Test flow:
1) Wait for PTY, ping.
2) Emit two deterministic events with distinct snapshots (via set_state + event-log-mark).
3) Read back the log and verify ordering, values, and count.
4) Reboot the app (debug reboot) and confirm the log persists.
"""

import os
import sys
import time

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


def wait_for_ping(client: UARTClient, tries: int = 15, delay: float = 0.1) -> None:
    for _ in range(tries):
        try:
            client.ping()
            return
        except Exception:
            time.sleep(delay)
    raise ProtocolError("ping did not recover")


def emit_event(client: UARTClient, rtype: int, speed: int, batt_dv: int, batt_da: int, temp_dc: int, power_w: int) -> None:
    client.set_state(
        rpm=0,
        torque=0,
        speed_dmph=speed,
        soc=80,
        err=0,
        power_w=power_w,
        batt_dV=batt_dv,
        batt_dA=batt_da,
        ctrl_temp_dC=temp_dc,
    )
    client.log_event_mark(rtype, flags=0x01)


def main() -> int:
    try:
        wait_for_pty(PORT)
    except FileNotFoundError as e:
        sys.stderr.write(str(e) + "\n")
        return 1

    client = UARTClient(PORT, baud=115200, timeout=0.5)
    try:
        wait_for_ping(client)
        before = client.event_log_summary()

        emit_event(client, 1, speed=123, batt_dv=125, batt_da=-30, temp_dc=250, power_w=450)
        emit_event(client, 2, speed=234, batt_dv=118, batt_da=-20, temp_dc=310, power_w=520)

        recs = client.event_log_read(offset=before.count, limit=4)
        expect(len(recs) == 2, f"expected 2 new records, got {len(recs)}")
        expect(recs[0].type == 1 and recs[1].type == 2, "type order incorrect")
        expect(recs[0].speed_dmph == 123 and recs[1].speed_dmph == 234, "speed mismatch")
        expect(recs[0].batt_dV == 125 and recs[1].batt_dV == 118, "voltage mismatch")
        expect(recs[0].cmd_power_w == 450 and recs[1].cmd_power_w == 520, "power snapshot mismatch")

        # Soft reboot via config commit (reuses existing command path)
        try:
            cfg = client.config_get()
            cfg.seq += 1
            client.config_stage(cfg)
            client.config_commit(reboot=True)
        except ProtocolError:
            pass
        time.sleep(0.2)
        wait_for_ping(client)

        after = client.event_log_summary()
        expect(after.count >= before.count + 2, "events did not persist across reboot")
        recs_after = client.event_log_read(offset=after.count - 2, limit=2)
        expect(len(recs_after) == 2, "tail read length mismatch")
        expect(recs_after[0].type == 1 and recs_after[1].type == 2, "post-reboot order mismatch")

        print("PASS: event log append/read/persist")
        return 0
    except (AssertionError, ProtocolError) as e:
        sys.stderr.write(f"FAIL: {e}\n")
        return 1
    finally:
        client.close()


if __name__ == "__main__":
    raise SystemExit(main())
