#!/usr/bin/env python3
"""
Listen to UART1 BLE notifications and print raw bytes as hex/ascii.
"""

import argparse
import asyncio
import binascii
import sys

try:
    from bleak import BleakClient
except ImportError:
    print("Install bleak: pip install bleak", file=sys.stderr)
    sys.exit(1)

NUS_SERVICE = "0000ffe0-0000-1000-8000-00805f9b34fb"
NUS_RX = "0000ffe9-0000-1000-8000-00805f9b34fb"  # write (unused)
NUS_TX = "0000ffe4-0000-1000-8000-00805f9b34fb"  # notify


def _format_ascii(data: bytes) -> str:
    out = []
    for b in data:
        if 32 <= b <= 126:
            out.append(chr(b))
        elif b == 10:
            out.append("\\n")
        elif b == 13:
            out.append("\\r")
        else:
            out.append(".")
    return "".join(out)


async def main():
    ap = argparse.ArgumentParser(description="Listen for UART1 BLE notifications")
    ap.add_argument("mac", help="BLE MAC address (or UUID on macOS/iOS)")
    ap.add_argument("--service", default=NUS_SERVICE, help="UART service UUID")
    ap.add_argument("--rx", default=NUS_RX, help="UART RX characteristic (write)")
    ap.add_argument("--tx", default=NUS_TX, help="UART TX characteristic (notify)")
    ap.add_argument("--seconds", type=float, default=5.0, help="listen duration")
    args = ap.parse_args()

    def on_notify(_handle, data: bytes):
        hx = binascii.hexlify(data).decode()
        asc = _format_ascii(data)
        print(f"[notify] {hx} | {asc}")

    client = BleakClient(args.mac)
    await client.connect()
    if hasattr(client, "get_services"):
        await client.get_services()
    else:
        _ = client.services
    await client.start_notify(args.tx, on_notify)
    try:
        await asyncio.sleep(args.seconds)
    finally:
        await client.stop_notify(args.tx)
        await client.disconnect()


if __name__ == "__main__":
    asyncio.run(main())
