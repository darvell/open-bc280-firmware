#!/usr/bin/env python3
"""
BLE OTA helper for BC280-class displays.

Flow:
 1) Connect to the *app* firmware over BLE (normal mode).
 2) Send CMD 0x20 (enter bootloader).
 3) Device reboots; reconnect (same MAC) now running bootloader.
 4) Bootloader cmds:
      0x00 init
      0x22 init FW (crc8, size)
      0x24 write 128-byte blocks
      0x26 complete (crc check & reset)

Frame format: 0x55 | CMD | LEN | PAYLOAD | CHKSUM, where CHKSUM = bitwise-not XOR of all prior bytes.

Defaults target the common "Nordic UART" service used by the display BLE module:
  Service: 6e400001-b5a3-f393-e0a9-e50e24dcca9e
  TX char (notify): 6e400003-b5a3-f393-e0a9-e50e24dcca9e
  RX char (write):  6e400002-b5a3-f393-e0a9-e50e24dcca9e

If your device exposes different UUIDs, pass --service/--rx/--tx.
"""

import argparse
import asyncio
import binascii
import math
import os
import sys
from typing import Optional

try:
    from bleak import BleakClient
except ImportError:
    print("Install bleak: pip install bleak", file=sys.stderr)
    sys.exit(1)

NUS_SERVICE = "6e400001-b5a3-f393-e0a9-e50e24dcca9e"
NUS_RX = "6e400002-b5a3-f393-e0a9-e50e24dcca9e"  # write
NUS_TX = "6e400003-b5a3-f393-e0a9-e50e24dcca9e"  # notify


def crc8(data: bytes, poly: int = 0x07, init: int = 0x00) -> int:
    """CRC-8 over firmware image. Default poly 0x07, init 0x00 (matches observe calc_crc8 in bootloader)."""
    crc = init & 0xFF
    for b in data:
        crc ^= b
        for _ in range(8):
            if crc & 0x80:
                crc = ((crc << 1) ^ poly) & 0xFF
            else:
                crc = (crc << 1) & 0xFF
    return crc


def pack_frame(cmd: int, payload: bytes = b"") -> bytes:
    if len(payload) > 255:
        raise ValueError("payload too long")
    hdr = bytes([0x55, cmd & 0xFF, len(payload) & 0xFF])
    x = 0
    for b in hdr + payload:
        x ^= b
    cks = (~x) & 0xFF
    return hdr + payload + bytes([cks])


def chunk_blocks(image: bytes, block_size: int = 0x80):
    for i in range(0, len(image), block_size):
        yield i // block_size, image[i : i + block_size]


class OtaSession:
    def __init__(self, mac: str, service: str, rx_uuid: str, tx_uuid: str, image: bytes, verbose: bool, crc_poly: int, crc_init: int):
        self.mac = mac
        self.service = service
        self.rx_uuid = rx_uuid
        self.tx_uuid = tx_uuid
        self.image = image
        self.verbose = verbose
        self.rx_queue = asyncio.Queue()
        self.crc_poly = crc_poly
        self.crc_init = crc_init

    async def _connect(self) -> BleakClient:
        client = BleakClient(self.mac)
        await client.connect()
        await client.start_notify(self.tx_uuid, self._on_notify)
        return client

    def _on_notify(self, _handle, data: bytes):
        if self.verbose:
            print(f"[notify] {binascii.hexlify(data).decode()}")
        self.rx_queue.put_nowait(data)

    async def _write(self, client: BleakClient, frame: bytes):
        if self.verbose:
            print(f"[tx] {binascii.hexlify(frame).decode()}")
        await client.write_gatt_char(self.rx_uuid, frame, response=True)

    async def _expect(self, cmd: int, timeout: float = 2.0) -> bytes:
        try:
            while True:
                data = await asyncio.wait_for(self.rx_queue.get(), timeout=timeout)
                if len(data) >= 2 and data[1] == cmd:
                    return data
        except asyncio.TimeoutError:
            raise RuntimeError(f"Timeout waiting for cmd 0x{cmd:02X}")

    async def enter_bootloader(self):
        print("[*] Connecting to APP and asking for bootloader...")
        client = await self._connect()
        try:
            await self._write(client, pack_frame(0x20))
            await asyncio.sleep(0.5)
        finally:
            await client.disconnect()
        print("[*] Device should reboot into bootloader; waiting 3s to re-advertise")
        await asyncio.sleep(3.0)

    async def program_bootloader(self):
        print("[*] Connecting in bootloader mode...")
        client = await self._connect()
        try:
            # init
            await self._write(client, pack_frame(0x00))
            await self._expect(0x01)

            fw_size = len(self.image)
            fw_crc = crc8(self.image, self.crc_poly, self.crc_init)
            print(f"[*] FW size={fw_size} bytes crc8=0x{fw_crc:02X}")

            init_payload = bytes([fw_crc]) + fw_size.to_bytes(4, "big")
            await self._write(client, pack_frame(0x22, init_payload))
            resp = await self._expect(0x23)
            if len(resp) < 4 or resp[3] != 1:
                raise RuntimeError("bootloader rejected init")

            total_blocks = math.ceil(fw_size / 0x80)
            for idx, block in chunk_blocks(self.image):
                blk_payload = idx.to_bytes(4, "big") + block.ljust(0x80, b"\xFF")
                await self._write(client, pack_frame(0x24, blk_payload))
                r = await self._expect(0x25)
                if len(r) < 4 or r[3] != 0:
                    raise RuntimeError(f"block {idx} write failed")
                if self.verbose and idx % 8 == 0:
                    print(f"    wrote block {idx+1}/{total_blocks}")

            await self._write(client, pack_frame(0x26))
            final = await self._expect(0x27, timeout=5.0)
            if len(final) < 4 or final[3] != 1:
                raise RuntimeError("CRC check failed at finalize")
            print("[*] OTA complete; bootloader will reset to new app")
        finally:
            await client.disconnect()


async def main():
    ap = argparse.ArgumentParser(description="Push open-firmware over BLE OTA")
    ap.add_argument("mac", help="BLE MAC address (or UUID on macOS/iOS)")
    ap.add_argument("--bin", default="open-firmware/build/open_firmware.bin", help="firmware bin path")
    ap.add_argument("--service", default=NUS_SERVICE, help="UART service UUID")
    ap.add_argument("--rx", default=NUS_RX, help="UART RX characteristic (write)")
    ap.add_argument("--tx", default=NUS_TX, help="UART TX characteristic (notify)")
    ap.add_argument("-v", "--verbose", action="store_true", help="verbose I/O")
    ap.add_argument("--crc-poly", type=lambda s: int(s, 0), default=0x07, help="CRC8 poly (default 0x07)")
    ap.add_argument("--crc-init", type=lambda s: int(s, 0), default=0x00, help="CRC8 init (default 0x00)")
    args = ap.parse_args()

    if not os.path.isfile(args.bin):
        print(f"Firmware not found: {args.bin}", file=sys.stderr)
        sys.exit(1)

    with open(args.bin, "rb") as f:
        image = f.read()

    sess = OtaSession(args.mac, args.service, args.rx, args.tx, image, args.verbose, args.crc_poly, args.crc_init)
    await sess.enter_bootloader()
    await sess.program_bootloader()


if __name__ == "__main__":
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        pass
