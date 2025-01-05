#!/usr/bin/env python3
"""
Discover an Aventon bike in OEM mode and send the bootloader entry command (0x20).

Uses OEM BLE packet format:
  0x55 | CMD | LEN | PAYLOAD | CHKSUM
  CHKSUM = bitwise-not XOR of all prior bytes

Command:
  0x20 enter bootloader (payload none)
"""

import argparse
import asyncio
import binascii
import sys
import re
from typing import Optional, Tuple

try:
    from bleak import BleakClient, BleakScanner
except ImportError:
    print("Install bleak: pip install bleak", file=sys.stderr)
    sys.exit(1)

AVENTON_SERVICE_UUID = "0000ffe0-0000-1000-8000-00805f9b34fb"
AVENTON_WRITE_UUID = "0000ffe9-0000-1000-8000-00805f9b34fb"
AVENTON_NOTIFY_UUID = "0000ffe4-0000-1000-8000-00805f9b34fb"


def pack_frame(cmd: int, payload: bytes = b"") -> bytes:
    if len(payload) > 255:
        raise ValueError("payload too long")
    hdr = bytes([0x55, cmd & 0xFF, len(payload) & 0xFF])
    x = 0
    for b in hdr + payload:
        x ^= b
    cks = (~x) & 0xFF
    return hdr + payload + bytes([cks])


def _looks_like_aventon_name(name: str) -> bool:
    # OEM pattern: e.g. "Tv610u-123456" (prefix 6 chars, dash, digits)
    return bool(re.match(r"^[A-Za-z0-9]{6}-\\d+$", name))


async def _find_device(name_hint: Optional[str], timeout: float) -> Tuple[str, Optional[str]]:
    """Return (address, name) of the first matching device by name."""
    print(f"[*] Scanning for Aventon devices ({timeout:.1f}s)...")
    devices = await BleakScanner.discover(timeout=timeout)
    name_hint_l = name_hint.lower() if name_hint else None

    for d in devices:
        name = d.name or ""
        if name_hint_l and name_hint_l in name.lower():
            return d.address, name
        if not name_hint_l and _looks_like_aventon_name(name):
            return d.address, name

    raise RuntimeError("No matching BLE device found")


async def _send_enter_bootloader(
    address: str,
    write_uuid: str,
    notify_uuid: str,
    verbose: bool,
    connect_timeout: float,
    write_timeout: float,
) -> None:
    frame = pack_frame(0x20)
    if verbose:
        print(f"[tx] {binascii.hexlify(frame).decode()}")
    client = BleakClient(address, timeout=connect_timeout)
    await client.connect()
    try:
        # Some devices require notifications enabled for command processing.
        try:
            await asyncio.wait_for(client.start_notify(notify_uuid, lambda *_: None), timeout=write_timeout)
        except Exception:
            pass
        await asyncio.wait_for(client.write_gatt_char(write_uuid, frame, response=True), timeout=write_timeout)
    finally:
        await client.disconnect()


async def main():
    ap = argparse.ArgumentParser(description="Enter Aventon bootloader (OEM BLE mode)")
    ap.add_argument("--address", help="BLE address/UUID (skip scan)")
    ap.add_argument("--name", help="device name hint for scan")
    ap.add_argument("--timeout", type=float, default=10.0, help="scan timeout seconds")
    ap.add_argument("--service", default=AVENTON_SERVICE_UUID, help="Aventon service UUID")
    ap.add_argument("--write", default=AVENTON_WRITE_UUID, help="write characteristic UUID")
    ap.add_argument("--notify", default=AVENTON_NOTIFY_UUID, help="notify characteristic UUID")
    ap.add_argument("--connect-timeout", type=float, default=10.0, help="connect timeout seconds")
    ap.add_argument("--write-timeout", type=float, default=5.0, help="write/notify timeout seconds")
    ap.add_argument("-v", "--verbose", action="store_true", help="verbose output")
    args = ap.parse_args()

    if args.address:
        address = args.address
        name = None
    else:
        address, name = await _find_device(args.name, args.timeout)

    print(f"[*] Using device: {address}" + (f" ({name})" if name else ""))
    await _send_enter_bootloader(
        address,
        args.write,
        args.notify,
        args.verbose,
        args.connect_timeout,
        args.write_timeout,
    )
    print("[*] Bootloader entry command sent (0x20). Device should reboot into BLE update mode.")


if __name__ == "__main__":
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        pass
