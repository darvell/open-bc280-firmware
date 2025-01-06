#!/usr/bin/env python3
"""Renode regression for crash dump capture + clear.

Assumptions:
- Renode exposes UART1 via BC280_UART1_PTY (e.g. /tmp/uart1).
- Firmware built with RENODE_TEST=1 so crash trigger hook is available.
"""

import os
import sys
import time

from uart_client import CRASH_DUMP_MAGIC, ProtocolError, UARTClient

PORT = os.environ.get("BC280_UART1_PTY", "/tmp/uart1")


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


def wait_for_ping(client: UARTClient, tries: int = 30, delay: float = 0.2) -> None:
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

        client.crash_dump_clear()
        dump = client.crash_dump_read()
        expect(dump.magic == 0, "crash dump not cleared")

        pc_hint = client.crash_dump_trigger()
        time.sleep(0.2)
        wait_for_ping(client, tries=40, delay=0.25)

        dump = client.crash_dump_read()
        expect(dump.magic == CRASH_DUMP_MAGIC, "crash dump missing")
        expect(dump.crc_ok, "crash dump CRC invalid")
        expect(dump.sp != 0 and dump.lr != 0 and dump.pc != 0, "missing fault registers")
        if pc_hint:
            expect(dump.pc >= pc_hint and dump.pc < pc_hint + 0x80,
                   f"pc 0x{dump.pc:08x} not near hint 0x{pc_hint:08x}")

        client.crash_dump_clear()
        dump2 = client.crash_dump_read()
        expect(dump2.magic == 0, "crash dump not cleared after reset")

        print("PASS: crash dump capture + clear")
        return 0
    except (AssertionError, ProtocolError) as e:
        sys.stderr.write(f"FAIL: {e}\n")
        return 1
    finally:
        client.close()


if __name__ == "__main__":
    raise SystemExit(main())
