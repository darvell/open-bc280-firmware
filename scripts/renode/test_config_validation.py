#!/usr/bin/env python3
"""Renode regression for config validation guardrails.

Checks that invalid configs are rejected, logged with reason codes, and
that the firmware stays responsive (ping) afterward.
"""

import os
import sys
import time

from uart_client import UARTClient, ProtocolError, ConfigBlob

PORT = os.environ.get("BC280_UART1_PTY", "/tmp/uart1")

# Mirror firmware reasons in config_reject_reason_t
CFG_REJECT_RANGE = 1
CFG_REJECT_MONOTONIC = 2
CFG_REJECT_RATE = 3


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


def last_event(client: UARTClient):
    summary = client.event_log_summary()
    if summary.count == 0:
        return None, summary
    offset = summary.count - 1
    recs = client.event_log_read(offset, limit=1)
    if not recs:
        return None, summary
    return recs[0], summary


def reject_and_log(client: UARTClient, cfg: ConfigBlob, reason: int, note: str) -> None:
    before = client.event_log_summary()
    try:
        client.config_stage(cfg)
        raise AssertionError(f"{note}: invalid config accepted")
    except ProtocolError:
        pass
    client.ping()  # firmware stays responsive
    rec, after = last_event(client)
    expect(after.count == before.count + 1, f"{note}: event log not incremented")
    expect(rec is not None, f"{note}: missing event record")
    expect(rec.type == 7, f"{note}: wrong event type {rec.type}")
    expect(rec.flags == reason, f"{note}: wrong reason {rec.flags} != {reason}")


def main() -> int:
    try:
        wait_for_pty(PORT)
    except FileNotFoundError as e:
        sys.stderr.write(str(e) + "\n")
        return 1

    client = UARTClient(PORT, baud=115200, timeout=0.5)
    try:
        client.ping()

        # Case 1: out-of-range current limit
        cfg = ConfigBlob.defaults()
        cfg.cap_current_dA = 400  # 40 A, above allowed max
        reject_and_log(client, cfg, CFG_REJECT_RANGE, "overcurrent")

        # Case 2: non-monotonic assist curve
        cfg = ConfigBlob.defaults()
        cfg.curve_count = 3
        cfg.curve[0] = (0, 100)
        cfg.curve[1] = (50, 200)
        cfg.curve[2] = (40, 300)  # x decreases -> invalid
        reject_and_log(client, cfg, CFG_REJECT_MONOTONIC, "non-monotonic curve")

        # Case 3: logging rate too fast
        cfg = ConfigBlob.defaults()
        cfg.log_period_ms = 50  # below minimum
        reject_and_log(client, cfg, CFG_REJECT_RATE, "logging period")

        # Case 4: soft-start ramp below minimum
        cfg = ConfigBlob.defaults()
        cfg.soft_start_ramp_wps = 10
        reject_and_log(client, cfg, CFG_REJECT_RANGE, "soft-start ramp")

        # Case 5: soft-start deadband above max
        cfg = ConfigBlob.defaults()
        cfg.soft_start_deadband_w = 500
        reject_and_log(client, cfg, CFG_REJECT_RANGE, "soft-start deadband")

        # Case 6: soft-start kick above max
        cfg = ConfigBlob.defaults()
        cfg.soft_start_kick_w = 800
        reject_and_log(client, cfg, CFG_REJECT_RANGE, "soft-start kick")

        print("PASS: config validation rejects with reasoned events")
        return 0
    except (AssertionError, ProtocolError) as e:
        sys.stderr.write(f"FAIL: {e}\n")
        return 1
    finally:
        client.close()


if __name__ == "__main__":
    raise SystemExit(main())
