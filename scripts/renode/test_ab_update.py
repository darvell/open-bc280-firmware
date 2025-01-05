#!/usr/bin/env python3
"""Renode regression for A/B update slot activation.

Assumes Renode exposes UART1 via BC280_UART1_PTY (e.g. /tmp/uart1) and the
firmware was built from this repo (Meson/Ninja; see scripts/build_open_firmware.sh).
"""

import os
import sys
import time
import struct

from uart_client import ProtocolError, UARTClient, crc32

PORT = os.environ.get("BC280_UART1_PTY", "/tmp/uart1")

SPI_FLASH_BASE = 0x00300000
AB_SLOT_MAGIC = 0x4142534C
AB_SLOT_VERSION = 1
AB_SLOT_HEADER_SIZE = 32
AB_SLOT_STRIDE = 0x00040000
AB_SLOT0_BASE = SPI_FLASH_BASE + 0x00014000
AB_SLOT1_BASE = AB_SLOT0_BASE + AB_SLOT_STRIDE


def expect(cond: bool, msg: str) -> None:
    if not cond:
        raise AssertionError(msg)


def slot_base(slot: int) -> int:
    return AB_SLOT0_BASE if slot == 0 else AB_SLOT1_BASE


def build_header(build_id: int, payload: bytes) -> bytes:
    header = struct.pack(">IHHIIIIII",
        AB_SLOT_MAGIC,
        AB_SLOT_VERSION,
        AB_SLOT_HEADER_SIZE,
        len(payload),
        crc32(payload),
        build_id,
        0,
        0,
        0,
    )
    return header


def wait_for_pty(path: str, timeout_s: float = 5.0) -> None:
    deadline = time.time() + timeout_s
    while time.time() < deadline:
        if os.path.exists(path):
            return
        time.sleep(0.1)
    raise FileNotFoundError(f"UART PTY not found at {path}")


def wait_for_ping(client: UARTClient, tries: int = 20, delay: float = 0.1) -> None:
    for _ in range(tries):
        try:
            client.ping()
            return
        except Exception:
            time.sleep(delay)
    raise ProtocolError("ping did not recover after reboot")


def main() -> int:
    try:
        wait_for_pty(PORT)
    except FileNotFoundError as e:
        sys.stderr.write(str(e) + "\n")
        return 1

    client = UARTClient(PORT, baud=115200, timeout=0.5)
    try:
        wait_for_ping(client)
        st = client.ab_status()
        active = st.active_slot if st.active_slot in (0, 1) else 0
        target = 1 - active
        build_id = 0xA1B2C3D4

        payload = bytes([i & 0xFF for i in range(256)])
        header = build_header(build_id, payload)
        base = slot_base(target)
        client.write_mem(base, header)
        client.write_mem(base + AB_SLOT_HEADER_SIZE, payload)

        client.ab_set_pending(target)
        client.reboot_bootloader()
        time.sleep(0.2)
        wait_for_ping(client)

        st2 = client.ab_status()
        expect(st2.active_slot == target, f"active slot did not switch to {target}")
        expect(st2.build_id == build_id, "build_id mismatch after update")
        expect(st2.pending_slot == 0xFF, "pending slot not cleared")
        expect(st2.last_good_slot == active, "last_good_slot mismatch")

        print("PASS: A/B update activation")
        return 0
    except (AssertionError, ProtocolError) as e:
        sys.stderr.write(f"FAIL: {e}\n")
        return 1
    finally:
        client.close()


if __name__ == "__main__":
    raise SystemExit(main())
