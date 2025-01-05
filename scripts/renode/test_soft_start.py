#!/usr/bin/env python3
"""Renode regression for soft-start / launch control ramp limiter.

Scenarios:
- stop -> start transition ramps output monotonically at configured rate
- initial kick cap is respected
- brake cancels ramp immediately

Requires Renode UART1 PTY at $BC280_UART1_PTY (defaults /tmp/uart1) and
firmware built from the standard open-firmware image (no special build flags).
"""

import copy
import os
import sys
import time

from uart_client import ProtocolError, UARTClient

PORT = os.environ.get("BC280_UART1_PTY", "/tmp/uart1")

RAMP_WPS = 200
DEADBAND_W = 10
KICK_W = 60


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


def apply_soft_start_config(client: UARTClient) -> None:
    cfg = client.config_get()
    cfg.soft_start_ramp_wps = RAMP_WPS
    cfg.soft_start_deadband_w = DEADBAND_W
    cfg.soft_start_kick_w = KICK_W
    cfg.seq += 1
    client.config_stage(cfg)
    client.config_commit(reboot=False)


def send_state(client: UARTClient, brake: int = 0) -> None:
    client.set_state(
        rpm=0,
        torque=200,
        speed_dmph=150,
        soc=90,
        err=0,
        cadence_rpm=80,
        throttle_pct=60,
        brake=brake,
        buttons=0,
    )


def main() -> int:
    try:
        wait_for_pty(PORT)
    except FileNotFoundError as e:
        sys.stderr.write(str(e) + "\n")
        return 1

    client = UARTClient(PORT, baud=115200, timeout=0.5)
    orig_cfg = None
    try:
        client.ping()
        orig_cfg = copy.deepcopy(client.config_get())
        apply_soft_start_config(client)

        # Ensure ramp starts from idle.
        client.set_state(rpm=0, torque=0, speed_dmph=0, soc=0, err=0,
                         cadence_rpm=0, throttle_pct=0, brake=0, buttons=0)
        idle = client.debug_state()
        expect(idle.soft_start_active == 0, "soft-start should be inactive at idle")
        expect(idle.cmd_power_w == 0, "idle cmd_power_w should be 0")

        # Start ramp.
        send_state(client, brake=0)
        first = client.debug_state()
        expect(first.soft_start_active == 1, "soft-start should be active on launch")
        expect(first.soft_start_output_w > 0, "soft-start output should be >0")
        expect(first.soft_start_output_w <= min(KICK_W, first.soft_start_target_w),
               "kick cap not respected")
        target = first.soft_start_target_w
        prev_ms = first.ms
        prev_out = first.soft_start_output_w

        # Ramp up over a few steps, checking monotonic + slope.
        for _ in range(6):
            time.sleep(0.2)
            send_state(client, brake=0)
            st = client.debug_state()
            expect(st.soft_start_target_w == target, "target should remain stable during ramp")
            expect(st.soft_start_output_w >= prev_out, "ramp output should be monotonic")
            dt = st.ms - prev_ms
            max_step = (RAMP_WPS * dt) // 1000 + 1
            expect((st.soft_start_output_w - prev_out) <= max_step,
                   f"ramp step too large: {st.soft_start_output_w - prev_out} > {max_step}")
            expect(st.soft_start_output_w <= target, "ramp output should not exceed target")
            prev_ms = st.ms
            prev_out = st.soft_start_output_w

        # Brake cancels ramp immediately.
        send_state(client, brake=1)
        br = client.debug_state()
        expect(br.brake == 1, "brake flag should be set")
        expect(br.cmd_power_w == 0, "cmd_power_w not zeroed on brake")
        expect(br.soft_start_active == 0, "soft-start should cancel on brake")
        expect(br.soft_start_output_w == 0, "soft-start output should reset on brake")

        print("PASS: soft-start ramp + kick + brake cancel")
        return 0
    except (ProtocolError, AssertionError) as e:
        sys.stderr.write(f"FAIL: {e}\n")
        return 1
    finally:
        if orig_cfg is not None:
            try:
                orig_cfg.seq += 1
                client.config_stage(orig_cfg)
                client.config_commit(reboot=False)
            except Exception:
                pass
        client.close()


if __name__ == "__main__":
    raise SystemExit(main())
