#!/usr/bin/env python3
"""Renode regression for brake override safety interlock.

Scenarios covered:
- Assist: brake asserted while propulsion active zeros command within one tick and logs an event.
- Walk assist: active walk is cancelled and output forced to zero on brake.
- Cruise: cruise_state cleared and output zeroed on brake.

Requires Renode UART1 PTY at $BC280_UART1_PTY (defaults /tmp/uart1) and
firmware built with RENODE_TEST=1 for deterministic traces.
"""

import os
import sys
import time

from uart_client import ProtocolError, UARTClient, ConfigBlob

PORT = os.environ.get("BC280_UART1_PTY", "/tmp/uart1")


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


def set_walk_cap(client: UARTClient, enabled: bool) -> None:
    cfg = client.config_get()
    cfg.flags = (cfg.flags | 0x01) if enabled else (cfg.flags & ~0x01)
    cfg.seq += 1
    client.config_stage(cfg)
    client.config_commit(reboot=False)


def latest_brake_events(client: UARTClient, offset: int) -> int:
    recs = client.event_log_read(offset=offset, limit=4)
    return sum(1 for r in recs if r.type == 1)


def main() -> int:
    try:
        wait_for_pty(PORT)
    except FileNotFoundError as e:
        sys.stderr.write(str(e) + "\n")
        return 1

    client = UARTClient(PORT, baud=115200, timeout=0.5)
    try:
        client.ping()
        ev_before = client.event_log_summary()

        # --- Assist path ---
        client.set_state(0, 150, 80, 90, 0, cadence_rpm=0, throttle_pct=25, brake=0, buttons=0)
        dbg = client.debug_state()
        expect(dbg.cmd_power_w > 0, "assist command should be non-zero before brake")
        expect(dbg.brake == 0, "brake flag should be clear initially")

        client.set_state(0, 150, 80, 90, 0, cadence_rpm=0, throttle_pct=25, brake=1, buttons=0)
        dbg_brake = client.debug_state()
        expect(dbg_brake.brake == 1, "brake flag should latch")
        expect(dbg_brake.cmd_power_w == 0 and dbg_brake.cmd_current_dA == 0, "assist output not zeroed by brake")
        expect(dbg_brake.assist_mode == 0, "assist_mode should clear on brake")

        # --- Walk assist path ---
        set_walk_cap(client, True)
        client.set_state(0, 20, 0, 90, 0, cadence_rpm=0, throttle_pct=0, brake=0, buttons=0x40)
        dbg_walk = client.debug_state()
        expect(dbg_walk.walk_state == 1, f"walk not active: {dbg_walk.walk_state}")
        expect(dbg_walk.walk_cmd_power_w > 0, "walk command should be >0 before brake")

        client.set_state(0, 20, 0, 90, 0, cadence_rpm=0, throttle_pct=0, brake=1, buttons=0x40)
        dbg_walk_brake = client.debug_state()
        expect(dbg_walk_brake.brake == 1, "brake flag should be set during walk test")
        expect(dbg_walk_brake.walk_cmd_power_w == 0, "walk power should zero on brake")
        expect(dbg_walk_brake.walk_state == 2, "walk should be cancelled by brake")
        expect(dbg_walk_brake.assist_mode == 0, "outputs should be cleared under brake")

        # --- Cruise path (engage then brake cancel) ---
        client.set_state(0, 120, 80, 90, 0, cadence_rpm=0, throttle_pct=12, brake=0, buttons=0x00)
        client.set_state(0, 120, 80, 90, 0, cadence_rpm=0, throttle_pct=12, brake=0, buttons=0x80)
        client.set_state(0, 120, 80, 90, 0, cadence_rpm=0, throttle_pct=0, brake=0, buttons=0x00)
        dbg_cruise = client.debug_state()
        expect(dbg_cruise.cruise_state != 0, "cruise_state should be active after engage")
        expect(dbg_cruise.cmd_power_w > 0, "cruise test needs non-zero output")

        client.set_state(0, 120, 80, 90, 0, cadence_rpm=0, throttle_pct=0, brake=1, buttons=0x00)
        dbg_cruise_brake = client.debug_state()
        expect(dbg_cruise_brake.cmd_power_w == 0 and dbg_cruise_brake.cmd_current_dA == 0, "cruise output not zeroed by brake")
        expect(dbg_cruise_brake.cruise_state == 0, "cruise should clear on brake")

        # --- Event log check ---
        ev_after = client.event_log_summary()
        new_events = latest_brake_events(client, ev_before.count)
        expect(new_events >= 2, "brake events not logged")
        expect(ev_after.count >= ev_before.count + new_events, "event log count did not advance")

        print("PASS: brake override across assist/walk/cruise")
        return 0
    except (ProtocolError, AssertionError) as e:
        sys.stderr.write(f"FAIL: {e}\n")
        return 1
    finally:
        client.close()


if __name__ == "__main__":
    raise SystemExit(main())
