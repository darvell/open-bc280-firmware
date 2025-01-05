#!/usr/bin/env python3
"""
BLE OTA helper for BC280-class displays.

Flow:
 1) (Optional) Connect to the *app* firmware over BLE (normal mode).
 2) Send CMD 0x20 (enter bootloader).
 3) Device reboots; reconnect (same MAC) now running bootloader.
 4) Bootloader cmds:
      0x00 init
      0x22 init FW (crc8, size)
      0x24 write 128-byte blocks
      0x26 complete (crc check & reset)

Frame format: 0x55 | CMD | LEN | PAYLOAD | CHKSUM, where CHKSUM = bitwise-not XOR of all prior bytes.

Defaults target the Aventon OEM BLE service/characteristics:
  Service: 0000ffe0-0000-1000-8000-00805f9b34fb
  TX char (notify): 0000ffe4-0000-1000-8000-00805f9b34fb
  RX char (write):  0000ffe9-0000-1000-8000-00805f9b34fb

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

NUS_SERVICE = "0000ffe0-0000-1000-8000-00805f9b34fb"
NUS_RX = "0000ffe9-0000-1000-8000-00805f9b34fb"  # write
NUS_TX = "0000ffe4-0000-1000-8000-00805f9b34fb"  # notify


def crc8_bootloader(data: bytes, poly: int = 0x8C, init: int = 0x00) -> int:
    """CRC-8 used by OEM bootloader (reflected poly 0x8C, init 0x00, xorout 0x00)."""
    crc = init & 0xFF
    for b in data:
        crc ^= b
        for _ in range(8):
            if crc & 1:
                crc = ((crc >> 1) ^ poly) & 0xFF
            else:
                crc = (crc >> 1) & 0xFF
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
        # Ensure services are discovered on macOS before writes.
        if hasattr(client, "get_services"):
            await client.get_services()
        else:
            _ = client.services
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

    async def _write_with_reconnect(self, client: BleakClient, frame: bytes, connect_timeout: float, write_timeout: float) -> BleakClient:
        try:
            if hasattr(client, "get_services"):
                await client.get_services()
            await asyncio.wait_for(self._write(client, frame), timeout=write_timeout)
            return client
        except Exception as e:
            # Retry once after reconnect if services aren't ready or link dropped
            if self.verbose:
                print(f"[warn] write failed, reconnecting: {e}")
            try:
                await client.disconnect()
            except Exception:
                pass
            await asyncio.sleep(0.3)
            client = BleakClient(self.mac, timeout=connect_timeout)
            await client.connect()
            if hasattr(client, "get_services"):
                await client.get_services()
            else:
                _ = client.services
            await client.start_notify(self.tx_uuid, self._on_notify)
            await asyncio.wait_for(self._write(client, frame), timeout=write_timeout)
            return client

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

    async def program_bootloader(
        self,
        block_timeout: float,
        block_retries: int,
        inter_block_ms: int,
        start_block: int,
        reconnect_every_blocks: int,
        reconnect_delay_ms: int,
        progress_every: int,
    ):
        print("[*] Connecting in bootloader mode...")
        client = await self._connect()
        try:
            fw_size = len(self.image)
            if fw_size % 4 != 0:
                pad = 4 - (fw_size % 4)
                if self.verbose:
                    print(f"[*] Padding image by {pad} bytes to satisfy CRC word alignment")
                self.image += b"\xFF" * pad
                fw_size = len(self.image)

            fw_crc = crc8_bootloader(self.image, self.crc_poly, self.crc_init)
            print(f"[*] FW size={fw_size} bytes (4-byte aligned) crc8=0x{fw_crc:02X}")

            init_payload = bytes([fw_crc]) + fw_size.to_bytes(4, "big")
            init_ok = False
            for attempt in range(1, max(1, block_retries) + 1):
                try:
                    # init
                    client = await self._write_with_reconnect(client, pack_frame(0x00), block_timeout, block_timeout)
                    await self._expect(0x01, timeout=block_timeout)
                    client = await self._write_with_reconnect(client, pack_frame(0x22, init_payload), block_timeout, block_timeout)
                    resp = await self._expect(0x23, timeout=block_timeout)
                    if len(resp) < 4 or resp[3] != 1:
                        raise RuntimeError("bootloader rejected init")
                    init_ok = True
                    break
                except Exception as e:
                    if attempt >= block_retries:
                        raise RuntimeError(f"init failed after {block_retries} retries: {e}") from e
                    if self.verbose:
                        print(f"[warn] init failed, retrying ({attempt}/{block_retries}): {e}")
                    try:
                        await client.disconnect()
                    except Exception:
                        pass
                    await asyncio.sleep(0.3)
                    client = await self._connect()

            if not init_ok:
                raise RuntimeError("bootloader init failed")

            total_blocks = math.ceil(fw_size / 0x80)
            if start_block < 0 or start_block >= total_blocks:
                raise RuntimeError(f"start_block {start_block} out of range (0..{total_blocks-1})")

            sent_since_reconnect = 0
            for idx, block in chunk_blocks(self.image):
                if idx < start_block:
                    continue
                blk_payload = idx.to_bytes(4, "big") + block.ljust(0x80, b"\xFF")
                attempt = 0
                while True:
                    attempt += 1
                    try:
                        client = await self._write_with_reconnect(client, pack_frame(0x24, blk_payload), block_timeout, block_timeout)
                        r = await self._expect(0x25, timeout=block_timeout)
                        if len(r) < 4 or r[3] != 0:
                            raise RuntimeError(f"block {idx} write failed (status {r[3] if len(r) >= 4 else '??'})")
                        break
                    except Exception as e:
                        if attempt > block_retries:
                            raise RuntimeError(f"block {idx} write failed after {block_retries} retries: {e}") from e
                        if self.verbose:
                            print(f"    retry block {idx} (attempt {attempt}/{block_retries})")
                        await asyncio.sleep(0.05)
                if (progress_every and ((idx + 1) % progress_every == 0)) or (idx + 1 == total_blocks):
                    print(f"[*] wrote block {idx+1}/{total_blocks}")
                if inter_block_ms:
                    await asyncio.sleep(inter_block_ms / 1000.0)
                sent_since_reconnect += 1
                if reconnect_every_blocks > 0 and sent_since_reconnect >= reconnect_every_blocks:
                    if self.verbose:
                        print(f"    reconnecting after {sent_since_reconnect} blocks")
                    await client.disconnect()
                    if reconnect_delay_ms:
                        await asyncio.sleep(reconnect_delay_ms / 1000.0)
                    client = await self._connect()
                    sent_since_reconnect = 0

            client = await self._write_with_reconnect(client, pack_frame(0x26), block_timeout, block_timeout)
            final = await self._expect(0x27, timeout=block_timeout)
            if len(final) < 4 or final[3] != 1:
                raise RuntimeError("CRC check failed at finalize")
            print("[*] OTA complete; bootloader will reset to new app")
        finally:
            await client.disconnect()


async def main():
    ap = argparse.ArgumentParser(description="Push open-firmware over BLE OTA")
    ap.add_argument("mac", help="BLE MAC address (or UUID on macOS/iOS)")
    ap.add_argument("--bin", default="build/open_firmware.bin", help="firmware bin path")
    ap.add_argument("--service", default=NUS_SERVICE, help="UART service UUID")
    ap.add_argument("--rx", default=NUS_RX, help="UART RX characteristic (write)")
    ap.add_argument("--tx", default=NUS_TX, help="UART TX characteristic (notify)")
    ap.add_argument("-v", "--verbose", action="store_true", help="verbose I/O")
    ap.add_argument("--assume-bootloader", action="store_true", help="skip app->bootloader jump; already in BLE update mode")
    ap.add_argument("--block-timeout", type=float, default=6.0, help="seconds to wait per block ack/response")
    ap.add_argument("--block-retries", type=int, default=3, help="retries per block before failing")
    ap.add_argument("--inter-block-ms", type=int, default=10, help="delay between blocks (ms)")
    ap.add_argument("--start-block", type=int, default=0, help="start from block index (for resume)")
    ap.add_argument("--reconnect-every-blocks", type=int, default=0, help="disconnect/reconnect every N blocks (0=disabled)")
    ap.add_argument("--reconnect-delay-ms", type=int, default=300, help="delay before reconnect (ms)")
    ap.add_argument("--progress-every", type=int, default=25, help="print progress every N blocks (0=disable)")
    ap.add_argument("--crc-poly", type=lambda s: int(s, 0), default=0x8C, help="CRC8 poly (default 0x8C, bootloader)")
    ap.add_argument("--crc-init", type=lambda s: int(s, 0), default=0x00, help="CRC8 init (default 0x00)")
    args = ap.parse_args()

    if not os.path.isfile(args.bin):
        print(f"Firmware not found: {args.bin}", file=sys.stderr)
        sys.exit(1)

    with open(args.bin, "rb") as f:
        image = f.read()

    sess = OtaSession(args.mac, args.service, args.rx, args.tx, image, args.verbose, args.crc_poly, args.crc_init)
    if not args.assume_bootloader:
        await sess.enter_bootloader()
    await sess.program_bootloader(
        args.block_timeout,
        args.block_retries,
        args.inter_block_ms,
        args.start_block,
        args.reconnect_every_blocks,
        args.reconnect_delay_ms,
        args.progress_every,
    )


if __name__ == "__main__":
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        pass
