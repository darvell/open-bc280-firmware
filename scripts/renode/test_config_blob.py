#!/usr/bin/env python3
"""Renode regression for config blob staging/commit/rollback.

Assumes Renode exposes UART1 via BC280_UART1_PTY (e.g. /tmp/uart1) and the
firmware was built from this repo (Meson/Ninja; see scripts/build_open_firmware.sh).
"""

import os
import sys
import time

from uart_client import ProtocolError, UARTClient, ConfigBlob, crc32

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


def wait_for_ping(client: UARTClient, tries: int = 10, delay: float = 0.1) -> None:
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
        active = client.config_get()
        start_seq = active.seq

        # Stage + commit a valid config with reboot.
        new_cfg = ConfigBlob.defaults()
        new_cfg.wheel_mm = 2150
        new_cfg.units = 1
        new_cfg.profile_id = 3
        new_cfg.theme = 2
        new_cfg.flags = 0x01  # walk capability enabled, no unsupported bits
        new_cfg.seq = start_seq + 1
        client.config_stage(new_cfg)
        client.config_commit(reboot=True)

        # Allow reboot; then re-ping and verify persisted config.
        time.sleep(0.2)
        wait_for_ping(client)
        updated = client.config_get()
        expect(updated.seq == start_seq + 1, "sequence did not advance")
        expect(updated.wheel_mm == new_cfg.wheel_mm, "wheel_mm mismatch post reboot")
        expect(updated.units == new_cfg.units, "units mismatch")
        expect(updated.profile_id == new_cfg.profile_id, "profile_id mismatch")
        expect(updated.theme == new_cfg.theme, "theme mismatch")
        expect(updated.flags == new_cfg.flags, "flags mismatch")

        # Invalid config (out-of-range wheel_mm) should be rejected.
        bad_cfg = ConfigBlob.defaults()
        bad_cfg.wheel_mm = 50  # too small
        bad_cfg.seq = updated.seq + 1
        try:
            client.config_stage(bad_cfg)
            raise AssertionError("invalid config accepted")
        except ProtocolError:
            pass

        # Bad CRC should be rejected.
        cfg = ConfigBlob.defaults()
        cfg.wheel_mm = 2300
        cfg.seq = updated.seq + 1
        payload = cfg.to_payload(recalc_crc=True)
        # Corrupt CRC field.
        corrupt = bytearray(payload)
        corrupt[9] ^= 0xFF
        try:
            client._send(0x31, bytes(corrupt), expect_len=1)
            raise AssertionError("bad CRC config accepted")
        except ProtocolError:
            pass

        # Bootloader path should remain intact.
        client.reboot_bootloader()

        print("PASS: config commit/persist, invalid rejects, bootloader path")
        return 0
    except (AssertionError, ProtocolError) as e:
        sys.stderr.write(f"FAIL: {e}\n")
        return 1
    finally:
        client.close()


if __name__ == "__main__":
    raise SystemExit(main())
