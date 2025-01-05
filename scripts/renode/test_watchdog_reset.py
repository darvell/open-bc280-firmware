#!/usr/bin/env python3
"""Renode regression for watchdog reset + reset reason capture.

Assumptions:
- Renode exposes UART1 via BC280_UART1_PTY (e.g. /tmp/uart1).
- Firmware built from the standard open-firmware image (no special build flags).
"""

import os
import sys
import time

from uart_client import ProtocolError, UARTClient

PORT = os.environ.get("BC280_UART1_PTY", "/tmp/uart1")

EVT_RESET_REASON = 9
RESET_FLAG_IWDG = 1 << 4


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


def wait_for_ping(client: UARTClient, tries: int = 20, delay: float = 0.2) -> None:
    for _ in range(tries):
        try:
            client.ping()
            return
        except Exception:
            time.sleep(delay)
    raise ProtocolError("ping did not recover")


def main() -> int:
    try:
        wait_for_pty(PORT)
    except FileNotFoundError as e:
        sys.stderr.write(str(e) + "\n")
        return 1

    client = UARTClient(PORT, baud=115200, timeout=0.5)
    try:
        wait_for_ping(client)
        before = client.event_log_summary().count
        start = client.debug_state().ms

        status = client.fault_inject(0x10)
        expect(status == 0, "fault_inject(watchdog) status failed")

        reset_seen = False
        deadline = time.time() + 6.0
        while time.time() < deadline:
            try:
                st = client.debug_state()
            except ProtocolError:
                time.sleep(0.1)
                continue
            if st.ms < start or st.ms < 500:
                reset_seen = True
                if st.reset_flags & RESET_FLAG_IWDG:
                    break
            time.sleep(0.2)

        expect(reset_seen, "watchdog reset not observed")

        st = client.debug_state()
        expect(st.reset_flags & RESET_FLAG_IWDG, "reset_flags missing IWDG bit")

        after = client.event_log_summary().count
        expect(after >= before + 1, "reset reason event not logged")
        recs = client.event_log_read(offset=after - 1, limit=1)
        expect(recs and recs[0].type == EVT_RESET_REASON, "reset reason event type mismatch")
        expect(recs[0].flags & RESET_FLAG_IWDG, "reset reason flags missing IWDG bit")

        print("PASS: watchdog reset + reset reason capture")
        return 0
    except (AssertionError, ProtocolError) as e:
        sys.stderr.write(f"FAIL: {e}\n")
        return 1
    finally:
        client.close()


if __name__ == "__main__":
    raise SystemExit(main())
