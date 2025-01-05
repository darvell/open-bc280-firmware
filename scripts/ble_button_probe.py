#!/usr/bin/env python3
"""
Interactive BLE button probe for BC280 open-firmware.

Connects to the BLE UART service and reads GPIOC IDR (0x40011008) via the
debug read_mem command (0x04). Prompts the user for each button and prints
the raw IDR plus active-low pressed mask.
"""

import argparse
import asyncio
import binascii
import sys
from typing import List, Optional

try:
    from bleak import BleakClient, BleakScanner
except ImportError:
    print("Install bleak: pip install bleak", file=sys.stderr)
    sys.exit(1)

NUS_SERVICE = "0000ffe0-0000-1000-8000-00805f9b34fb"
NUS_RX = "0000ffe9-0000-1000-8000-00805f9b34fb"  # write
NUS_TX = "0000ffe4-0000-1000-8000-00805f9b34fb"  # notify

SOF = 0x55
CMD_READ_MEM = 0x04
RESP_READ_MEM = CMD_READ_MEM | 0x80

GPIOC_IDR_ADDR = 0x40011008


def pack_frame(cmd: int, payload: bytes) -> bytes:
    if len(payload) > 255:
        raise ValueError("payload too long")
    hdr = bytes([SOF, cmd & 0xFF, len(payload) & 0xFF])
    x = 0
    for b in hdr + payload:
        x ^= b
    cks = (~x) & 0xFF
    return hdr + payload + bytes([cks])


class FrameParser:
    def __init__(self):
        self.buf = bytearray()

    def feed(self, data: bytes) -> List[bytes]:
        self.buf.extend(data)
        out = []
        while True:
            if len(self.buf) < 4:
                break
            if self.buf[0] != SOF:
                del self.buf[0]
                continue
            plen = self.buf[2]
            total = 4 + plen
            if len(self.buf) < total:
                break
            frame = bytes(self.buf[:total])
            del self.buf[:total]
            x = 0
            for b in frame[:-1]:
                x ^= b
            if ((~x) & 0xFF) != frame[-1]:
                continue
            out.append(frame)
        return out


class BleProbe:
    def __init__(self, address: str, rx_uuid: str, tx_uuid: str, verbose: bool):
        self.address = address
        self.rx_uuid = rx_uuid
        self.tx_uuid = tx_uuid
        self.verbose = verbose
        self.parser = FrameParser()
        self.frames: asyncio.Queue = asyncio.Queue()
        self.client: Optional[BleakClient] = None

    def _on_notify(self, _handle, data: bytes):
        if self.verbose:
            print(f"[notify] {binascii.hexlify(data).decode()}")
        for frame in self.parser.feed(data):
            self.frames.put_nowait(frame)

    async def connect(self, timeout: float):
        self.client = BleakClient(self.address, timeout=timeout)
        await self.client.connect()
        await self.client.start_notify(self.tx_uuid, self._on_notify)

    async def disconnect(self):
        if self.client and self.client.is_connected:
            await self.client.disconnect()
        self.client = None

    async def read_mem(self, addr: int, size: int, timeout: float,
                       retries: int = 5, reconnect_delay: float = 0.5,
                       connect_timeout: float = 10.0) -> bytes:
        payload = addr.to_bytes(4, "big") + bytes([size & 0xFF])
        frame = pack_frame(CMD_READ_MEM, payload)
        attempt = 0
        while True:
            try:
                if not self.client or not self.client.is_connected:
                    await self.connect(connect_timeout)
                if self.verbose:
                    print(f"[tx] {binascii.hexlify(frame).decode()}")
                await self.client.write_gatt_char(self.rx_uuid, frame, response=True)
                while True:
                    fr = await asyncio.wait_for(self.frames.get(), timeout=timeout)
                    if len(fr) >= 4 and fr[1] == RESP_READ_MEM and fr[2] == size:
                        return fr[3 : 3 + size]
            except Exception as exc:
                attempt += 1
                if attempt > retries:
                    raise RuntimeError(f"BLE read failed after {retries} retries: {exc}") from exc
                try:
                    await self.disconnect()
                except Exception:
                    pass
                if self.verbose:
                    print(f"[warn] read failed, reconnecting ({attempt}/{retries})")
                await asyncio.sleep(reconnect_delay)


async def find_device(name_hint: Optional[str], timeout: float) -> str:
    devices = await BleakScanner.discover(timeout=timeout)
    if not name_hint:
        name_hint = "Tv610"
    name_hint_l = name_hint.lower()
    for d in devices:
        name = d.name or ""
        if name_hint_l in name.lower():
            return d.address
    raise RuntimeError("No matching BLE device found")


def format_bits(v: int, width: int = 8) -> str:
    return "".join("1" if (v & (1 << i)) else "0" for i in range(width - 1, -1, -1))


async def main() -> None:
    ap = argparse.ArgumentParser(description="Interactive BC280 button probe over BLE")
    ap.add_argument("--address", help="BLE address/UUID (skip scan)")
    ap.add_argument("--name", help="device name hint for scan", default="Tv610")
    ap.add_argument("--scan-timeout", type=float, default=8.0, help="scan timeout seconds")
    ap.add_argument("--connect-timeout", type=float, default=10.0, help="connect timeout seconds")
    ap.add_argument("--read-timeout", type=float, default=2.0, help="read timeout seconds")
    ap.add_argument("--retries", type=int, default=5, help="BLE read retries before abort")
    ap.add_argument("--reconnect-delay", type=float, default=0.5, help="seconds to wait before reconnect")
    ap.add_argument("--mask", type=lambda s: int(s, 0), default=0x1F, help="button bit mask (default 0x1F for PC0-4)")
    ap.add_argument("-v", "--verbose", action="store_true", help="verbose I/O")
    args = ap.parse_args()

    if args.address:
        address = args.address
    else:
        address = await find_device(args.name, args.scan_timeout)
    print(f"[*] Using device: {address}")

    probe = BleProbe(address, NUS_RX, NUS_TX, args.verbose)
    await probe.connect(args.connect_timeout)
    try:
        buttons = [
            "UP",
            "DOWN",
            "INFO",
            "POWER",
            "LIGHT",
            "UP+DOWN",
            "UP+LIGHT",
            "DOWN+LIGHT",
            "POWER+INFO",
            "POWER+UP",
            "POWER+DOWN",
            "UP+DOWN+LIGHT",
            "POWER+UP+DOWN",
            "ALL",
        ]
        print("Hold the requested button, then press Enter to sample.")
        print("Type 'q' then Enter to quit.")
        for label in buttons:
            resp = input(f"[hold] {label} > ").strip().lower()
            if resp == "q":
                break
            data = await probe.read_mem(
                GPIOC_IDR_ADDR,
                2,
                args.read_timeout,
                retries=args.retries,
                reconnect_delay=args.reconnect_delay,
                connect_timeout=args.connect_timeout,
            )
            idr = int.from_bytes(data, "little")
            pressed = (~idr) & (args.mask & 0xFFFF)
            print(f"{label}: GPIOC IDR=0x{idr:04X} (bits {format_bits(idr, 16)}) pressed=0x{pressed:02X}")
    finally:
        await probe.disconnect()


if __name__ == "__main__":
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        pass
