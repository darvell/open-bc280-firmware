#!/usr/bin/env python3
"""Renode regression for adaptive assist heuristics (effort-constant + eco bias).

Scenarios:
- Disabled: adaptive fields clear, no boost on speed drop.
- Effort-constant: speed drop + power rise boosts output within bounds.
- Eco bias: rapid speed spike clamps output change (rate limiting).
"""

import os
import sys
import time

from uart_client import ProtocolError, UARTClient

PORT = os.environ.get("BC280_UART1_PTY", "/tmp/uart1")

CFG_FLAG_ADAPT_EFFORT = 1 << 5
CFG_FLAG_ADAPT_ECO = 1 << 6

ADAPT_EFFORT_MIN_ERR_DMPH = 8
ADAPT_EFFORT_GAIN_W_PER_DMPH = 2
ADAPT_EFFORT_MAX_BOOST_W = 180
ADAPT_EFFORT_MAX_BOOST_Q15 = 16384

ADAPT_ECO_RATE_SPIKE_WPS = 120


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


def set_flags(client: UARTClient, enable: int, disable: int) -> None:
    cfg = client.config_get()
    cfg.flags |= enable
    cfg.flags &= ~disable
    client.config_stage(cfg)
    client.config_commit(reboot=False)


def send_state(client: UARTClient, speed_dmph: int, throttle: int, torque: int,
               power_w: int, batt_dV: int = 420, batt_dA: int = 80) -> None:
    client.set_state(
        0,
        torque,
        speed_dmph,
        90,
        0,
        cadence_rpm=80,
        throttle_pct=throttle,
        brake=0,
        buttons=0,
        power_w=power_w,
        batt_dV=batt_dV,
        batt_dA=batt_dA,
    )


def base_power(throttle: int, torque: int) -> int:
    return (throttle * 8) + (torque // 4)


def main() -> int:
    if not wait_for_pty(PORT):
        sys.stderr.write(f"UART PTY not found at {PORT}\n")
        return 1

    client = UARTClient(PORT, baud=115200, timeout=0.5)
    try:
        client.ping()

        # Disabled: ensure flags cleared
        set_flags(client, enable=0, disable=(CFG_FLAG_ADAPT_EFFORT | CFG_FLAG_ADAPT_ECO))

        throttle = 20
        torque = 40
        p_base = base_power(throttle, torque)
        speed_high = 180
        speed_low = 120

        for _ in range(5):
            send_state(client, speed_high, throttle, torque, power_w=170)
            time.sleep(0.2)
        dbg_base = client.debug_state()

        send_state(client, speed_low, throttle, torque, power_w=200)
        time.sleep(0.2)
        dbg_drop = client.debug_state()

        expect(dbg_drop.cmd_power_w <= dbg_base.cmd_power_w + 2,
               "disabled: cmd_power_w should not increase on speed drop")
        expect(dbg_drop.adaptive_speed_delta_dmph == 0,
               "disabled: adaptive_speed_delta_dmph should be 0")
        expect(dbg_drop.adaptive_trend_active == 0,
               "disabled: adaptive_trend_active should be 0")
        expect(dbg_drop.adaptive_clamp_active == 0,
               "disabled: adaptive_clamp_active should be 0")

        # Effort-constant: enable effort only
        set_flags(client, enable=CFG_FLAG_ADAPT_EFFORT, disable=CFG_FLAG_ADAPT_ECO)

        for _ in range(6):
            send_state(client, speed_high, throttle, torque, power_w=180)
            time.sleep(0.2)
        warm = client.debug_state()

        send_state(client, speed_low, throttle, torque, power_w=240)
        time.sleep(0.2)
        boosted = client.debug_state()

        max_boost = min(
            ADAPT_EFFORT_MAX_BOOST_W,
            (p_base * ADAPT_EFFORT_MAX_BOOST_Q15 + (1 << 14)) >> 15,
        )
        expect(boosted.adaptive_speed_delta_dmph > ADAPT_EFFORT_MIN_ERR_DMPH,
               "effort: speed delta too small")
        expect(boosted.adaptive_trend_active == 1,
               "effort: trend flag not asserted")
        expect(boosted.cmd_power_w > warm.cmd_power_w,
               "effort: cmd_power_w did not increase")
        expect(boosted.cmd_power_w <= p_base + max_boost + 2,
               "effort: cmd_power_w exceeded boost cap")

        # Eco bias: enable eco only
        set_flags(client, enable=CFG_FLAG_ADAPT_ECO, disable=CFG_FLAG_ADAPT_EFFORT)

        throttle_low = 10
        throttle_high = 50
        speed_low = 100
        speed_high = 170

        send_state(client, speed_low, throttle_low, torque, power_w=90)
        time.sleep(0.2)
        slow = client.debug_state()

        send_state(client, speed_high, throttle_high, torque, power_w=320)
        time.sleep(0.2)
        fast = client.debug_state()

        dt_ms = fast.ms - slow.ms
        if dt_ms == 0:
            dt_ms = 1
        max_rise = (ADAPT_ECO_RATE_SPIKE_WPS * dt_ms) // 1000 + 5
        expect(fast.adaptive_clamp_active == 1,
               "eco: clamp flag not asserted")
        expect((fast.cmd_power_w - slow.cmd_power_w) <= max_rise,
               "eco: cmd_power_w rise exceeded rate limit")

        print("PASS: adaptive assist heuristics")
        return 0
    except (ProtocolError, AssertionError) as e:
        sys.stderr.write(f"FAIL: {e}\n")
        return 1
    finally:
        client.close()


if __name__ == "__main__":
    raise SystemExit(main())
