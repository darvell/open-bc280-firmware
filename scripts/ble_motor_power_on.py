#!/usr/bin/env python3
"""
Enable motor control GPIOs over BLE debug protocol in safe mode.

Uses CMD 0x02 (read32) and CMD 0x03 (write32) to configure:
  - PA11/PA12 outputs (PA12 high, PA11 low)
  - PB12 output high
  - PB5/PB6 outputs high
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
NUS_RX = "0000ffe9-0000-1000-8000-00805f9b34fb"  # write
NUS_TX = "0000ffe4-0000-1000-8000-00805f9b34fb"  # notify

CMD_READ32 = 0x02
CMD_WRITE32 = 0x03

RCC_APB2ENR = 0x40021018
GPIOA_BASE = 0x40010800
GPIOB_BASE = 0x40010C00
GPIO_CRL = 0x00
GPIO_CRH = 0x04
GPIO_BSRR = 0x10
GPIO_BRR = 0x14

RCC_APB2ENR_IOPA = 1 << 2
RCC_APB2ENR_IOPB = 1 << 3


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

    def feed(self, data: bytes):
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


class BleRw32:
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

    async def connect(self) -> BleakClient:
        client = BleakClient(self.mac)
        await client.connect()
        if hasattr(client, "get_services"):
            await client.get_services()
        else:
            _ = client.services
        await client.start_notify(self.tx_uuid, self._on_notify)
        return client

    async def write_frame(self, client: BleakClient, frame: bytes):
        if self.verbose:
            print(f"[tx] {binascii.hexlify(frame).decode()}")
        await client.write_gatt_char(self.rx_uuid, frame, response=True)

    async def read32(self, client: BleakClient, addr: int, timeout: float) -> int:
        payload = addr.to_bytes(4, "big")
        await self.write_frame(client, pack_frame(CMD_READ32, payload))
        while True:
            frame = await asyncio.wait_for(self.frames.get(), timeout=timeout)
            if len(frame) >= 7 and frame[1] == (CMD_READ32 | 0x80) and frame[2] == 4:
                return int.from_bytes(frame[3:7], "big")

    async def write32(self, client: BleakClient, addr: int, value: int, timeout: float) -> None:
        payload = addr.to_bytes(4, "big") + value.to_bytes(4, "big")
        await self.write_frame(client, pack_frame(CMD_WRITE32, payload))
        while True:
            frame = await asyncio.wait_for(self.frames.get(), timeout=timeout)
            if len(frame) >= 4 and frame[1] == (CMD_WRITE32 | 0x80):
                return


def _set_pin_mode(word: int, pin: int, mode: int) -> int:
    shift = (pin % 8) * 4
    return (word & ~(0xF << shift)) | ((mode & 0xF) << shift)


async def main():
    ap = argparse.ArgumentParser(description="Enable motor control GPIOs over BLE debug protocol")
    ap.add_argument("mac", help="BLE MAC address (or UUID on macOS/iOS)")
    ap.add_argument("--service", default=NUS_SERVICE, help="UART service UUID")
    ap.add_argument("--rx", default=NUS_RX, help="UART RX characteristic (write)")
    ap.add_argument("--tx", default=NUS_TX, help="UART TX characteristic (notify)")
    ap.add_argument("--timeout", type=float, default=2.0, help="seconds to wait per response")
    ap.add_argument("-v", "--verbose", action="store_true", help="verbose I/O")
    args = ap.parse_args()

    rw = BleRw32(args.mac, args.service, args.rx, args.tx, args.verbose)
    client = await rw.connect()
    try:
        # Enable GPIOA/GPIOB clocks.
        apb2 = await rw.read32(client, RCC_APB2ENR, args.timeout)
        apb2 |= (RCC_APB2ENR_IOPA | RCC_APB2ENR_IOPB)
        await rw.write32(client, RCC_APB2ENR, apb2, args.timeout)

        # Configure PA11/PA12 (CRH pins 11/12) as 2MHz push-pull (mode=0x2).
        gpioa_crh_addr = GPIOA_BASE + GPIO_CRH
        crh = await rw.read32(client, gpioa_crh_addr, args.timeout)
        crh = _set_pin_mode(crh, 11, 0x2)
        crh = _set_pin_mode(crh, 12, 0x2)
        await rw.write32(client, gpioa_crh_addr, crh, args.timeout)

        # Configure PB12 (CRH pin 12) as 2MHz push-pull.
        gpiob_crh_addr = GPIOB_BASE + GPIO_CRH
        crh_b = await rw.read32(client, gpiob_crh_addr, args.timeout)
        crh_b = _set_pin_mode(crh_b, 12, 0x2)
        await rw.write32(client, gpiob_crh_addr, crh_b, args.timeout)

        # Configure PB5/PB6 (CRL pins 5/6) as 2MHz push-pull.
        gpiob_crl_addr = GPIOB_BASE + GPIO_CRL
        crl_b = await rw.read32(client, gpiob_crl_addr, args.timeout)
        crl_b = _set_pin_mode(crl_b, 5, 0x2)
        crl_b = _set_pin_mode(crl_b, 6, 0x2)
        await rw.write32(client, gpiob_crl_addr, crl_b, args.timeout)

        # Drive outputs: PA12 high, PA11 low; PB12/PB5/PB6 high.
        await rw.write32(client, GPIOA_BASE + GPIO_BSRR, (1 << 12), args.timeout)
        await rw.write32(client, GPIOA_BASE + GPIO_BRR, (1 << 11), args.timeout)
        await rw.write32(client, GPIOB_BASE + GPIO_BSRR, (1 << 12) | (1 << 5) | (1 << 6), args.timeout)

        print("OK: motor control pins configured + driven.")
    finally:
        if client.is_connected:
            await client.disconnect()


if __name__ == "__main__":
    asyncio.run(main())
