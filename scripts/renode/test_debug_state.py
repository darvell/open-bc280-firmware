#!/usr/bin/env python3
"""Renode regression for versioned debug state (cmd 0x21).

Assumes Renode provides UART1 over PTY (BC280_UART1_PTY). The test:
- sends an extended set_state frame (with cadence/throttle/brake/buttons)
- requests debug_state_v1
- asserts fields and derived outputs match expectations
- verifies legacy state_dump (0x0A) still works for backward compatibility

Usage:
  BC280_UART1_PTY=/tmp/uart1 ./scripts/renode/test_debug_state.py
"""

import os
import sys
import time

from uart_client import ProtocolError, UARTClient

PORT = os.environ.get("BC280_UART1_PTY", "/tmp/uart1")


def expect(cond: bool, msg: str) -> None:
    if not cond:
        raise AssertionError(msg)


def main() -> int:
    for _ in range(50):
        if os.path.exists(PORT):
            break
        time.sleep(0.1)
    if not os.path.exists(PORT):
        sys.stderr.write(f"UART PTY not found at {PORT}\n")
        return 1

    client = UARTClient(PORT, baud=115200, timeout=0.5)
    try:
        client.ping()

        # Extended input payload
        rpm = 300
        tq = 40
        spd = 150
        soc = 90
        err = 2
        cadence = 88
        throttle = 30
        brake = 0
        buttons = 0x05  # profile/gear bits (no cruise engage)

        client.set_state(rpm, tq, spd, soc, err,
                         cadence_rpm=cadence,
                         throttle_pct=throttle,
                         brake=brake,
                         buttons=buttons)

        dbg = client.debug_state()
        expect(dbg.version >= 1, "debug version mismatch")
        expect(dbg.size >= 28, "debug size mismatch")
        expect(dbg.speed_dmph == spd, "speed mismatch")
        expect(dbg.cadence_rpm == cadence, "cadence mismatch")
        expect(dbg.torque_raw == tq, "torque mismatch")
        expect(dbg.throttle_pct == throttle, "throttle mismatch")
        expect(dbg.brake == brake, "brake mismatch")
        expect(dbg.buttons == buttons, "buttons mismatch")

        expect(dbg.assist_mode == 1, "assist_mode should be active")
        expect(dbg.profile_id == (buttons & 0x03), "profile_id mismatch")
        expect(dbg.virtual_gear == ((buttons >> 2) & 0x0F), "virtual_gear mismatch")
        expect(dbg.cruise_state == 0, "cruise_state mismatch")
        base_power = (throttle * 8) + (tq // 4)
        expect(dbg.cmd_power_w <= base_power, "cmd_power_w should clamp to curve/caps")
        expect(dbg.cmd_current_dA == dbg.cmd_power_w // 2, "cmd_current_dA mismatch")
        if dbg.version >= 2:
            expect(dbg.cap_power_w >= dbg.cmd_power_w, "cap_power_w too low")
            expect(dbg.cap_current_dA >= dbg.cmd_current_dA, "cap_current_dA too low")
            expect(dbg.cap_speed_dmph >= spd, "cap_speed_dmph mismatch")
        if dbg.version >= 3:
            expect(dbg.curve_power_w > 0, "curve_power_w missing")
            expect(dbg.curve_cadence_q15 > 0, "curve_cadence_q15 missing")
        expect(dbg.inputs_ms <= dbg.ms, "inputs timestamp after now")

        st = client.state_dump()
        expect(st.rpm == rpm, "legacy state rpm mismatch")
        expect(st.torque == tq, "legacy state torque mismatch")
        expect(st.speed_dmph == spd, "legacy state speed mismatch")

        print("PASS: debug_state fields + legacy state_dump")
        return 0
    except (ProtocolError, AssertionError) as e:
        sys.stderr.write(f"FAIL: {e}\n")
        return 1
    finally:
        client.close()


if __name__ == "__main__":
    raise SystemExit(main())
