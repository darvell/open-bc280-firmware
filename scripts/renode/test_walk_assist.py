#!/usr/bin/env python3
"""Renode regression for walk assist safety envelope.

Scenarios:
- engage walk, ramp speed toward cap -> command tapers to zero
- brake cancels immediately and requires re-engage
- capability flag off yields disabled state + zero output

Requires Renode UART1 PTY at $BC280_UART1_PTY (defaults /tmp/uart1).
"""

import os
import sys
import time

from uart_client import ProtocolError, UARTClient, ConfigBlob

PORT = os.environ.get("BC280_UART1_PTY", "/tmp/uart1")


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


def set_walk_cap(client: UARTClient, enabled: bool) -> None:
    cfg = client.config_get()
    cfg.flags = (cfg.flags | 0x01) if enabled else (cfg.flags & ~0x01)
    cfg.seq = cfg.seq + 1
    client.config_stage(cfg)
    client.config_commit(reboot=False)


def main() -> int:
    if not wait_for_pty(PORT):
        sys.stderr.write(f"UART PTY not found at {PORT}\n")
        return 1
    client = UARTClient(PORT, baud=115200, timeout=0.5)
    try:
        client.ping()

        # Ensure capability enabled.
        set_walk_cap(client, True)

        # Engage walk at low speed.
        client.set_state(0, 20, 0, 90, 0, cadence_rpm=0, throttle_pct=0, brake=0, buttons=0x40)
        dbg = client.debug_state()
        expect(dbg.walk_state == 1, f"walk not active: {dbg.walk_state}")
        base_cmd = dbg.walk_cmd_power_w
        expect(base_cmd > 0, "walk power should be >0")

        # Increase speed near cap: command should taper.
        client.set_state(0, 20, 35, 90, 0, cadence_rpm=0, throttle_pct=0, brake=0, buttons=0x40)
        dbg2 = client.debug_state()
        expect(dbg2.walk_cmd_power_w < base_cmd, "walk power did not taper near cap")

        # Hit cap: walk cancels/zeros.
        client.set_state(0, 20, 40, 90, 0, cadence_rpm=0, throttle_pct=0, brake=0, buttons=0x40)
        dbg3 = client.debug_state()
        expect(dbg3.walk_cmd_power_w == 0, "walk power should be zero at cap")
        expect(dbg3.walk_state == 2, "walk should be cancelled at cap")

        # Clear inhibit then test brake cancel.
        client.set_state(0, 0, 0, 90, 0, cadence_rpm=0, throttle_pct=0, brake=0, buttons=0x00)
        client.set_state(0, 0, 0, 90, 0, cadence_rpm=0, throttle_pct=0, brake=1, buttons=0x40)
        dbg4 = client.debug_state()
        expect(dbg4.walk_cmd_power_w == 0, "walk power should zero on brake")
        expect(dbg4.walk_state == 2, "walk should be cancelled by brake")

        # Disable capability and verify disabled state/output.
        set_walk_cap(client, False)
        client.set_state(0, 0, 0, 90, 0, cadence_rpm=0, throttle_pct=0, brake=0, buttons=0x40)
        dbg5 = client.debug_state()
        expect(dbg5.walk_state == 3, "walk should report disabled when cap off")
        expect(dbg5.walk_cmd_power_w == 0, "walk power should be zero when disabled")

        print("PASS: walk assist safety envelope")
        return 0
    except (ProtocolError, AssertionError) as e:
        sys.stderr.write(f"FAIL: {e}\n")
        return 1
    finally:
        client.close()


if __name__ == "__main__":
    raise SystemExit(main())
