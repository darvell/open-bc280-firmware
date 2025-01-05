#!/usr/bin/env python3
"""Renode regression for fault injection toggles (developer/test builds).

Assumptions:
- Renode exposes UART1 via BC280_UART1_PTY (e.g. /tmp/uart1).
- Firmware built from the standard open-firmware image (no special build flags).
"""

import os
import sys
import time

from uart_client import ProtocolError, UARTClient

PORT = os.environ.get("BC280_UART1_PTY", "/tmp/uart1")

EVT_COMM_FLAKY = 2
EVT_SENSOR_DROPOUT = 3
EVT_OVERTEMP_WARN = 4
EVT_DERATE_ACTIVE = 5


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


def has_event(records, rtype):
    return any(r.type == rtype and (r.flags & 0x80) for r in records)


def main() -> int:
    try:
        wait_for_pty(PORT)
    except FileNotFoundError as e:
        sys.stderr.write(str(e) + "\n")
        return 1

    client = UARTClient(PORT, baud=115200, timeout=0.5)
    try:
        wait_for_ping(client)

        # Speed sensor dropout -> outputs zeroed, event logged.
        before = client.event_log_summary().count
        status = client.fault_inject(0x01)
        expect(status == 0, "fault_inject(speed) status failed")
        client.set_state(rpm=120, torque=80, speed_dmph=150, soc=90, err=0, throttle_pct=30)
        st = client.debug_state()
        expect(st.fault_active & 0x01, "speed dropout not active")
        expect(st.speed_dmph == 0, "speed not dropped to 0")
        expect(st.cmd_power_w == 0, "cmd_power not zeroed on dropout")
        events = client.event_log_read(offset=before, limit=4)
        expect(has_event(events, EVT_SENSOR_DROPOUT), "sensor dropout event missing")

        # Comm errors -> retry counter bumps, flaky alert event at threshold.
        before = client.event_log_summary().count
        client.fault_inject(0x02, comm_retry_budget=2)
        for _ in range(3):
            client.set_state(rpm=0, torque=0, speed_dmph=50, soc=80, err=0)
        st = client.debug_state()
        expect(st.fault_active & 0x02, "comm fault not active")
        expect(st.comm_retry_count >= 2, "comm retry counter not incremented")
        expect(st.comm_flaky == 1, "comm flaky flag not set")
        events = client.event_log_read(offset=before, limit=4)
        expect(has_event(events, EVT_COMM_FLAKY), "comm flaky event missing")

        # Overtemp + derate -> temp forced high, power capped, events logged.
        before = client.event_log_summary().count
        client.fault_inject(0x0C, comm_retry_budget=2, derate_cap_w=150)
        client.set_state(
            rpm=0,
            torque=200,
            speed_dmph=180,
            soc=80,
            err=0,
            throttle_pct=100,
            ctrl_temp_dC=300,
            power_w=600,
        )
        st = client.debug_state()
        expect(st.fault_active & 0x04, "overtemp fault not active")
        expect(st.derate_cap_w == 150, "derate cap mismatch")
        expect(st.cmd_power_w <= 150, "derate cap not applied")
        events = client.event_log_read(offset=before, limit=6)
        expect(has_event(events, EVT_OVERTEMP_WARN), "overtemp event missing")
        expect(has_event(events, EVT_DERATE_ACTIVE), "derate event missing")

        # Disable injection -> mask cleared, active faults drop.
        client.fault_inject(0x00)
        client.set_state(rpm=0, torque=0, speed_dmph=100, soc=80, err=0)
        st = client.debug_state()
        expect(st.fault_active == 0, "faults not cleared when mask=0")

        print("PASS: fault injection toggles + logging")
        return 0
    except (AssertionError, ProtocolError) as e:
        sys.stderr.write(f"FAIL: {e}\n")
        return 1
    finally:
        client.close()


if __name__ == "__main__":
    raise SystemExit(main())
