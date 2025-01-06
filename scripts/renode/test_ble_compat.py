#!/usr/bin/env python3
"""Renode BLE compatibility encoder test (CSC/CPS/BAS).

This script expects Renode running headless with BC280_UART1_PTY set,
and a build compiled with BLE_COMPAT=1 and RENODE_TEST=1.
"""

import os
import sys
import time

from uart_client import ProtocolError, UARTClient

PORT = os.environ.get("BC280_UART1_PTY", "/tmp/uart1")
EVENT_TOL = 20  # ticks in 1/1024s (~19.5 ms)


def expect(cond: bool, msg: str) -> None:
    if not cond:
        raise AssertionError(msg)


def event_time_1024(ms: int) -> int:
    return int((ms * 1024 + 500) // 1000) & 0xFFFF


def diff_u16(a: int, b: int) -> int:
    return ((a - b + 0x8000) & 0xFFFF) - 0x8000


def main() -> int:
    # Wait briefly for the PTY to appear.
    for _ in range(50):
        if os.path.exists(PORT):
            break
        time.sleep(0.1)
    if not os.path.exists(PORT):
        sys.stderr.write(f"UART PTY not found at {PORT}\n")
        return 1

    client = UARTClient(PORT, baud=115200, timeout=0.5)
    cadence = 60
    soc = 87
    power = 250
    try:
        client.ping()

        # First update: establish BLE compat counters.
        client.set_state(
            rpm=120,
            torque=30,
            speed_dmph=0,
            soc=soc,
            err=0,
            cadence_rpm=cadence,
            power_w=power,
            batt_dV=512,
            batt_dA=-10,
            ctrl_temp_dC=400,
        )
        ms_a = client.debug_state().ms
        time.sleep(1.2)

        # Second update: advance cadence-based counters.
        client.set_state(
            rpm=120,
            torque=30,
            speed_dmph=0,
            soc=soc,
            err=0,
            cadence_rpm=cadence,
            power_w=power,
            batt_dV=512,
            batt_dA=-10,
            ctrl_temp_dC=400,
        )
        ms_b = client.debug_state().ms
        dt_ms = ms_b - ms_a
        expected_crank = int((cadence * dt_ms + 30000) // 60000)

        csc = client.ble_csc_measurement()
        expect((csc.flags & 0x03) == 0x03, "csc flags missing wheel/crank")
        expect(csc.wheel_revs == 0, "wheel revs should stay 0 at zero speed")
        expect(abs(csc.crank_revs - expected_crank) <= 1, "crank revs unexpected")
        evt = event_time_1024(ms_b)
        expect(abs(diff_u16(csc.wheel_event_time, evt)) <= EVENT_TOL, "csc wheel event time mismatch")
        expect(abs(diff_u16(csc.crank_event_time, evt)) <= EVENT_TOL, "csc crank event time mismatch")

        cps = client.ble_cps_measurement()
        expect((cps.flags & 0x0C) == 0x0C, "cps flags missing wheel/crank")
        expect(cps.instant_power == power, "cps power mismatch")
        expect(cps.wheel_revs == 0, "cps wheel revs should stay 0 at zero speed")
        expect(abs(cps.crank_revs - expected_crank) <= 1, "cps crank revs unexpected")
        expect(abs(diff_u16(cps.wheel_event_time, evt)) <= EVENT_TOL, "cps wheel event time mismatch")
        expect(abs(diff_u16(cps.crank_event_time, evt)) <= EVENT_TOL, "cps crank event time mismatch")

        bas = client.ble_bas_level()
        expect(bas == soc, "bas level mismatch")

        print("PASS: BLE CSC/CPS/BAS encoder payloads")
        return 0
    except ProtocolError as e:
        sys.stderr.write(f"Protocol error: {e}\n")
        return 1
    except AssertionError as e:
        sys.stderr.write(f"FAIL: {e}\n")
        return 1
    finally:
        client.close()


if __name__ == "__main__":
    raise SystemExit(main())
