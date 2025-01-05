#!/usr/bin/env python3
"""Renode regression for street/private mode guardrails and PIN gating.

Validates:
- street-legal mode rejects out-of-policy caps and logs an event
- correct PIN allows switching to private mode
- debug state reports mode + effective caps
- trace lines include a mode indicator (Renode test image)
"""

import dataclasses
import os
import sys
import time

from uart_client import ProtocolError, UARTClient

PORT = os.environ.get("BC280_UART1_PTY", "/tmp/uart1")
OUTDIR = os.environ.get("BC280_RENODE_OUTDIR") or os.path.abspath(
    os.path.join(os.path.dirname(__file__), "..", "..", "out", "renode")
)
UART_LOG = os.path.join(OUTDIR, "uart1_tx.log")

MODE_STREET = 0
MODE_PRIVATE = 1
LEGAL_MAX_CURRENT_DA = 200
LEGAL_MAX_SPEED_DMPH = 200

EVT_CONFIG_REJECT = 7
CFG_REJECT_POLICY = 6


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
    recs = client.event_log_read(summary.count - 1, limit=1)
    if not recs:
        return None, summary
    return recs[0], summary


def main() -> int:
    try:
        wait_for_pty(PORT)
    except FileNotFoundError as e:
        sys.stderr.write(str(e) + "\n")
        return 1

    # Reset UART log so trace assertions are deterministic.
    try:
        if os.path.exists(UART_LOG):
            os.remove(UART_LOG)
    except Exception:
        pass

    client = UARTClient(PORT, baud=115200, timeout=0.5)
    try:
        client.ping()
        active = client.config_get()

        # Ensure we start in street mode with legal caps.
        if active.mode != MODE_STREET:
            cfg = dataclasses.replace(active)
            cfg.mode = MODE_STREET
            cfg.pin_code = active.pin_code
            if cfg.cap_speed_dmph == 0 or cfg.cap_speed_dmph > LEGAL_MAX_SPEED_DMPH:
                cfg.cap_speed_dmph = LEGAL_MAX_SPEED_DMPH
            if cfg.cap_current_dA > LEGAL_MAX_CURRENT_DA:
                cfg.cap_current_dA = LEGAL_MAX_CURRENT_DA
            client.config_stage(cfg)
            client.config_commit(reboot=False)
            active = client.config_get()

        target_speed = LEGAL_MAX_SPEED_DMPH + 20  # above street cap, below profile cap

        # Street mode should reject out-of-policy cap.
        bad_cfg = dataclasses.replace(active)
        bad_cfg.mode = MODE_STREET
        bad_cfg.pin_code = active.pin_code
        bad_cfg.cap_speed_dmph = target_speed
        before = client.event_log_summary()
        try:
            client.config_stage(bad_cfg)
            raise AssertionError("out-of-policy config accepted in street mode")
        except ProtocolError:
            pass
        rec, after = last_event(client)
        expect(after.count >= before.count + 1, "event log did not increment")
        expect(rec is not None, "missing event record")
        expect(rec.type == EVT_CONFIG_REJECT, f"expected config reject event, got {rec.type}")
        expect(rec.flags == CFG_REJECT_POLICY, f"expected policy reject reason, got {rec.flags}")

        # Correct PIN unlocks private mode and allows higher caps.
        priv_cfg = dataclasses.replace(active)
        priv_cfg.mode = MODE_PRIVATE
        priv_cfg.pin_code = active.pin_code
        priv_cfg.cap_speed_dmph = target_speed
        client.config_stage(priv_cfg)
        client.config_commit(reboot=False)

        time.sleep(0.1)
        dbg = client.debug_state()
        expect(dbg.mode == MODE_PRIVATE, "debug state did not reflect private mode")
        expect(dbg.cap_effective_speed_dmph == target_speed,
               f"effective speed cap mismatch {dbg.cap_effective_speed_dmph} != {target_speed}")

        # Trace lines should always include the mode indicator.
        if os.path.exists(UART_LOG):
            with open(UART_LOG, "r", errors="ignore") as f:
                lines = [ln.strip() for ln in f.readlines() if "[TRACE]" in ln]
            expect(lines, "no trace lines found")
            expect(all("mode=" in ln for ln in lines), "trace missing mode indicator")

        print("PASS: mode guardrails + PIN gating + trace indicator")
        return 0
    except (AssertionError, ProtocolError) as e:
        sys.stderr.write(f"FAIL: {e}\n")
        return 1
    finally:
        client.close()


if __name__ == "__main__":
    raise SystemExit(main())
