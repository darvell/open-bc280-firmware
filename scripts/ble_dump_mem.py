#!/usr/bin/env python3
"""
Dump memory over BLE using the open-firmware UART1 debug protocol.

Use --flash to invoke the SPI flash read command (0x08) for external flash offsets.

Uses command 0x04 (read_mem):
  payload: addr[4] (big-endian), len[1]
  response: cmd|0x80 (0x84), payload len bytes

Frame format: 0x55 | CMD | LEN | PAYLOAD | CHKSUM
  CHKSUM = bitwise-not XOR of all prior bytes.
"""

import argparse
import asyncio
import binascii
import os
import sys
from typing import List

try:
    from bleak import BleakClient
except ImportError:
    print("Install bleak: pip install bleak", file=sys.stderr)
    sys.exit(1)

NUS_SERVICE = "0000ffe0-0000-1000-8000-00805f9b34fb"
NUS_RX = "0000ffe9-0000-1000-8000-00805f9b34fb"  # write
NUS_TX = "0000ffe4-0000-1000-8000-00805f9b34fb"  # notify

CMD_READ_MEM = 0x04
CMD_READ_FLASH = 0x08

RESP_READ_MEM = CMD_READ_MEM | 0x80
RESP_READ_FLASH = CMD_READ_FLASH | 0x80
CMD_SET_DEBUG_OUTPUT = 0x0F


def pack_frame(cmd: int, payload: bytes) -> bytes:
    if len(payload) > 255:
        raise ValueError("payload too long")
    hdr = bytes([0x55, cmd & 0xFF, len(payload) & 0xFF])
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
            if self.buf[0] != 0x55:
                del self.buf[0]
                continue
            payload_len = self.buf[2]
            frame_len = 4 + payload_len
            if len(self.buf) < frame_len:
                break
            frame = bytes(self.buf[:frame_len])
            del self.buf[:frame_len]
            if not self._valid_checksum(frame):
                continue
            out.append(frame)
        return out

    @staticmethod
    def _valid_checksum(frame: bytes) -> bool:
        x = 0
        for b in frame[:-1]:
            x ^= b
        return ((~x) & 0xFF) == frame[-1]


class BleMemDumper:
    def __init__(self, mac: str, service: str, rx_uuid: str, tx_uuid: str, verbose: bool):
        self.mac = mac
        self.service = service
        self.rx_uuid = rx_uuid
        self.tx_uuid = tx_uuid
        self.verbose = verbose
        self.parser = FrameParser()
        self.frames = asyncio.Queue()

    def _on_notify(self, _handle, data: bytes):
        if self.verbose:
            print(f"[notify] {binascii.hexlify(data).decode()}")
        for frame in self.parser.feed(data):
            self.frames.put_nowait(frame)

    async def _connect(self) -> BleakClient:
        client = BleakClient(self.mac)
        await client.connect()
        # Ensure services are discovered on macOS before writes.
        if hasattr(client, "get_services"):
            await client.get_services()
        else:
            _ = client.services
        await client.start_notify(self.tx_uuid, self._on_notify)
        return client

    async def _write(self, client: BleakClient, frame: bytes):
        if self.verbose:
            print(f"[tx] {binascii.hexlify(frame).decode()}")
        await client.write_gatt_char(self.rx_uuid, frame, response=True)

    async def read_block(self, client: BleakClient, addr: int, size: int, timeout: float, cmd: int) -> bytes:
        payload = addr.to_bytes(4, "big") + bytes([size & 0xFF])
        await self._write(client, pack_frame(cmd, payload))
        resp_cmd = (cmd | 0x80) & 0xFF
        while True:
            frame = await asyncio.wait_for(self.frames.get(), timeout=timeout)
            if len(frame) >= 4 and frame[1] == resp_cmd and frame[2] == size:
                return frame[3 : 3 + size]


async def main():
    ap = argparse.ArgumentParser(description="Dump memory over BLE (UART1 debug protocol)")
    ap.add_argument("mac", help="BLE MAC address (or UUID on macOS/iOS)")
    ap.add_argument("--start", type=lambda s: int(s, 0), required=True, help="start address (hex or dec)")
    ap.add_argument("--length", type=lambda s: int(s, 0), required=True, help="number of bytes to dump")
    ap.add_argument("--out", default="mem_dump.bin", help="output file")
    ap.add_argument("--service", default=NUS_SERVICE, help="UART service UUID")
    ap.add_argument("--rx", default=NUS_RX, help="UART RX characteristic (write)")
    ap.add_argument("--tx", default=NUS_TX, help="UART TX characteristic (notify)")
    ap.add_argument("--chunk", type=int, default=192, help="read size per request (<=192)")
    ap.add_argument("--flash", action="store_true",
                    help="use flash-read command (0x08) for external SPI flash offsets")
    ap.add_argument("--timeout", type=float, default=2.0, help="seconds to wait per response")
    ap.add_argument("--retries", type=int, default=20, help="reconnect attempts before giving up")
    ap.add_argument("--reconnect-delay", type=float, default=0.5, help="seconds to wait before reconnecting")
    ap.add_argument("--debug-mask", type=lambda s: int(s, 0), help="set debug UART output mask before dump")
    ap.add_argument("--quiet", action="store_true", help="disable debug UART output before dump")
    ap.add_argument("-v", "--verbose", action="store_true", help="verbose I/O")
    args = ap.parse_args()

    if args.chunk <= 0 or args.chunk > 192:
        print("chunk must be 1..192", file=sys.stderr)
        sys.exit(1)

    dumper = BleMemDumper(args.mac, args.service, args.rx, args.tx, args.verbose)
    remaining = args.length
    addr = args.start
    retries_left = args.retries
    cmd = CMD_READ_FLASH if args.flash else CMD_READ_MEM
    debug_mask = None
    if args.quiet:
        debug_mask = 0
    if args.debug_mask is not None:
        debug_mask = args.debug_mask & 0xFF

    os.makedirs(os.path.dirname(args.out) or ".", exist_ok=True)
    with open(args.out, "wb") as f:
        client = None
        while remaining > 0:
            try:
                if client is None or not client.is_connected:
                    client = await dumper._connect()
                    if debug_mask is not None:
                        await dumper._write(client, pack_frame(CMD_SET_DEBUG_OUTPUT, bytes([debug_mask])))
                        await asyncio.sleep(0.05)
                n = args.chunk if remaining > args.chunk else remaining
                data = await dumper.read_block(client, addr, n, args.timeout, cmd)
                f.write(data)
                addr += n
                remaining -= n
                if args.verbose:
                    print(f"dumped 0x{addr:08X} ({args.length - remaining}/{args.length})")
            except Exception as exc:
                if retries_left <= 0:
                    raise
                retries_left -= 1
                if args.verbose:
                    print(f"[warn] read failed at 0x{addr:08X}: {exc}; reconnecting ({retries_left} retries left)")
                try:
                    if client is not None and client.is_connected:
                        await client.disconnect()
                except Exception:
                    pass
                client = None
                await asyncio.sleep(args.reconnect_delay)

        if client is not None and client.is_connected:
            await client.disconnect()

    print(f"OK: wrote {args.length} bytes to {args.out}")


if __name__ == "__main__":
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        pass
