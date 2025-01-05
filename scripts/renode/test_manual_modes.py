#!/usr/bin/env python3
"""Renode regression for manual drive modes and sport boost budget.

Scenarios:
- manual current mode tracks setpoint until lugging governor constrains output
- manual power mode converges toward target and remains stable
- sport boost budget decreases under high phase current and recovers during cooldown
"""

import os
import sys
import time

from uart_client import ProtocolError, UARTClient

PORT = os.environ.get("BC280_UART1_PTY", "/tmp/uart1")

DRIVE_AUTO = 0
DRIVE_MANUAL_CURRENT = 1
DRIVE_MANUAL_POWER = 2
DRIVE_SPORT = 3

LIMIT_USER = 0
LIMIT_LUG = 1


def wait_for_pty(path: str, timeout: float = 5.0) -> bool:
    deadline = time.time() + timeout
    while time.time() < deadline:
        if os.path.exists(path):
            return True
        time.sleep(0.1)
    return False


def expect(cond: bool, msg: str) -> None:
    if not cond:
        raise AssertionError(msg)


def send_state(client: UARTClient, speed_dmph: int, batt_dV: int, batt_dA: int,
               torque: int = 120, throttle: int = 0) -> None:
    client.set_state(
        rpm=0,
        torque=torque,
        speed_dmph=speed_dmph,
        soc=90,
        err=0,
        cadence_rpm=80,
        throttle_pct=throttle,
        brake=0,
        buttons=0,
        power_w=0,
        batt_dV=batt_dV,
        batt_dA=batt_dA,
    )


def main() -> int:
    if not wait_for_pty(PORT):
        sys.stderr.write(f"UART PTY not found at {PORT}\n")
        return 1

    client = UARTClient(PORT, baud=115200, timeout=0.5)
    try:
        client.ping()

        # Manual current mode (tracks setpoint until lugging limit)
        setpoint_dA = 180
        client.set_drive_mode(DRIVE_MANUAL_CURRENT, setpoint_dA)
        send_state(client, speed_dmph=200, batt_dV=420, batt_dA=50)
        dbg = client.debug_state()
        expect(dbg.drive_mode == DRIVE_MANUAL_CURRENT, "drive mode not manual current")
        expect(dbg.limit_reason == LIMIT_USER, "manual current should start with USER limit")
        expect(abs(dbg.cmd_current_dA - setpoint_dA) <= 2, "cmd current not tracking setpoint")

        lug_vals = []
        for _ in range(10):
            send_state(client, speed_dmph=30, batt_dV=420, batt_dA=0)
            dbg = client.debug_state()
            lug_vals.append(dbg.cmd_power_w)
            time.sleep(0.2)
        expect(dbg.limit_reason == LIMIT_LUG, "lugging limiter did not engage in manual current mode")
        expect(dbg.cmd_power_w < setpoint_dA * 2, "lugging did not reduce manual current output")

        # Manual power mode (converges toward target)
        setpoint_w = 300
        client.set_drive_mode(DRIVE_MANUAL_POWER, setpoint_w)
        vals = []
        for _ in range(10):
            send_state(client, speed_dmph=200, batt_dV=420, batt_dA=30)
            dbg = client.debug_state()
            vals.append(dbg.cmd_power_w)
            time.sleep(0.2)
        expect(abs(vals[-1] - setpoint_w) <= 60, "manual power did not converge near target")
        expect(max(vals) <= setpoint_w * 2, "manual power runaway on low load")

        vals_hi = []
        for _ in range(8):
            send_state(client, speed_dmph=200, batt_dV=420, batt_dA=80)
            dbg = client.debug_state()
            vals_hi.append(dbg.cmd_power_w)
            time.sleep(0.2)
        expect(vals_hi[-1] <= setpoint_w * 2, "manual power runaway on high load")

        # Sport boost budget (burn + recover)
        client.set_drive_mode(DRIVE_SPORT, 0)
        send_state(client, speed_dmph=200, batt_dV=420, batt_dA=250)
        base = client.debug_state()
        budget_start = base.boost_budget_ms
        for _ in range(6):
            send_state(client, speed_dmph=200, batt_dV=420, batt_dA=250)
            time.sleep(0.2)
        burned = client.debug_state()
        expect(burned.boost_budget_ms < budget_start, "boost budget did not decrease under load")

        for _ in range(10):
            send_state(client, speed_dmph=200, batt_dV=420, batt_dA=10)
            time.sleep(0.2)
        recovered = client.debug_state()
        expect(recovered.boost_budget_ms >= burned.boost_budget_ms, "boost budget did not recover")

        print("PASS: manual drive modes + sport boost budget")
        return 0
    except (ProtocolError, AssertionError) as e:
        sys.stderr.write(f"FAIL: {e}\n")
        return 1
    finally:
        client.close()


if __name__ == "__main__":
    raise SystemExit(main())
