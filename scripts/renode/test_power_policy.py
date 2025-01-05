#!/usr/bin/env python3
"""Renode regression for multi-governor power policy (lugging/thermal/sag).

Scenarios:
- lugging limiter ramps down/up (monotonic, rate-limited)
- sag limiter activates under low voltage
- thermal proxy limiter activates under sustained high phase current
- final limit equals min(per-governor) and exposes limit_reason

Requires Renode UART1 PTY at $BC280_UART1_PTY (defaults /tmp/uart1).
"""

import os
import sys
import time

from uart_client import ProtocolError, UARTClient

PORT = os.environ.get("BC280_UART1_PTY", "/tmp/uart1")

LIMIT_USER = 0
LIMIT_LUG = 1
LIMIT_THERM = 2
LIMIT_SAG = 3


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
               throttle_pct: int = 20, torque: int = 160, temp_dC: int = None) -> None:
    kwargs = dict(
        cadence_rpm=80,
        throttle_pct=throttle_pct,
        brake=0,
        buttons=0,
        power_w=0,
        batt_dV=batt_dV,
        batt_dA=batt_dA,
    )
    if temp_dC is not None:
        kwargs["ctrl_temp_dC"] = temp_dC
    client.set_state(0, torque, speed_dmph, 90, 0, **kwargs)


def assert_min(dbg) -> None:
    min_allow = min(dbg.p_allow_user_w, dbg.p_allow_lug_w, dbg.p_allow_thermal_w, dbg.p_allow_sag_w)
    expect(dbg.p_allow_final_w == min_allow, "P_allow_final != min(P_allow_*)")


def main() -> int:
    if not wait_for_pty(PORT):
        sys.stderr.write(f"UART PTY not found at {PORT}\n")
        return 1

    client = UARTClient(PORT, baud=115200, timeout=0.5)
    try:
        client.ping()

        # Baseline (no limiting)
        send_state(client, speed_dmph=200, batt_dV=420, batt_dA=50)
        base = client.debug_state()
        expect(base.version >= 8 and base.size >= 78, "debug_state missing governor fields")
        assert_min(base)
        expect(base.limit_reason == LIMIT_USER, f"expected USER limit, got {base.limit_reason}")
        baseline_user = base.p_allow_user_w
        expect(baseline_user > 0, "baseline user power should be >0")

        # Lugging ramp down (low duty)
        lug_vals = []
        for _ in range(10):
            send_state(client, speed_dmph=30, batt_dV=420, batt_dA=0)
            dbg = client.debug_state()
            lug_vals.append(dbg.p_allow_lug_w)
            time.sleep(0.2)
        expect(all(lug_vals[i] >= lug_vals[i + 1] for i in range(len(lug_vals) - 1)),
               "lugging limit should ramp down monotonically")
        expect(lug_vals[-1] <= (baseline_user * 8 // 10), "lugging limit did not ramp down enough")
        dbg = client.debug_state()
        assert_min(dbg)
        expect(dbg.limit_reason == LIMIT_LUG, f"expected LUG limit, got {dbg.limit_reason}")
        expect(dbg.p_allow_final_w == dbg.p_allow_lug_w, "lugging should be active min")

        # Lugging recovery ramp up (higher duty)
        rec_vals = []
        for _ in range(6):
            send_state(client, speed_dmph=220, batt_dV=420, batt_dA=0)
            dbg = client.debug_state()
            rec_vals.append(dbg.p_allow_lug_w)
            time.sleep(0.2)
        expect(all(rec_vals[i] <= rec_vals[i + 1] for i in range(len(rec_vals) - 1)),
               "lugging recovery should ramp up monotonically")
        expect(rec_vals[-1] >= (baseline_user * 9 // 10), "lugging recovery too slow")

        # Sag limiter (low voltage)
        send_state(client, speed_dmph=220, batt_dV=330, batt_dA=50)
        sag = client.debug_state()
        assert_min(sag)
        expect(sag.limit_reason == LIMIT_SAG, f"expected SAG limit, got {sag.limit_reason}")
        expect(sag.p_allow_final_w == sag.p_allow_sag_w, "sag should be active min")

        # Thermal proxy limiter (sustained high current)
        therm = None
        for _ in range(15):
            send_state(client, speed_dmph=220, batt_dV=420, batt_dA=250)
            therm = client.debug_state()
            assert_min(therm)
            if therm.limit_reason == LIMIT_THERM:
                break
            time.sleep(0.2)
        expect(therm is not None and therm.limit_reason == LIMIT_THERM,
               f"thermal limiter did not engage (reason={therm.limit_reason if therm else 'none'})")
        expect(therm.p_allow_final_w == therm.p_allow_thermal_w, "thermal should be active min")

        print("PASS: power policy governors")
        return 0
    except (ProtocolError, AssertionError) as e:
        sys.stderr.write(f"FAIL: {e}\n")
        return 1
    finally:
        client.close()


if __name__ == "__main__":
    raise SystemExit(main())
