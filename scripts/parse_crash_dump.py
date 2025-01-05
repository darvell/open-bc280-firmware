#!/usr/bin/env python3
"""
Parse open-firmware crash dump blob (SPI flash).

Usage:
  uv run python scripts/parse_crash_dump.py crash_dump.bin
"""

from __future__ import annotations

import argparse
import binascii
import sys


CRASH_DUMP_MAGIC = 0x43525348  # 'CRSH'
CRASH_DUMP_VERSION = 1
CRASH_DUMP_EVENT_MAX = 4
CRASH_DUMP_HEADER_SIZE = 72
EVENT_LOG_RECORD_SIZE = 20
CRASH_DUMP_SIZE = CRASH_DUMP_HEADER_SIZE + (CRASH_DUMP_EVENT_MAX * EVENT_LOG_RECORD_SIZE)

OFF_MAGIC = 0
OFF_VERSION = 4
OFF_SIZE = 6
OFF_FLAGS = 8
OFF_SEQ = 12
OFF_CRC = 16
OFF_MS = 20
OFF_SP = 24
OFF_LR = 28
OFF_PC = 32
OFF_PSR = 36
OFF_CFSR = 40
OFF_HFSR = 44
OFF_DFSR = 48
OFF_MMFAR = 52
OFF_BFAR = 56
OFF_AFSR = 60
OFF_EVENT_COUNT = 64
OFF_EVENT_REC_SIZE = 66
OFF_EVENT_SEQ = 68
OFF_EVENT_RECORDS = 72


def be16(buf: bytes, off: int) -> int:
    return int.from_bytes(buf[off : off + 2], "big")


def be32(buf: bytes, off: int) -> int:
    return int.from_bytes(buf[off : off + 4], "big")


def crc32_compute(data: bytes) -> int:
    crc = 0xFFFFFFFF
    for b in data:
        crc ^= b
        for _ in range(8):
            mask = -(crc & 1) & 0xFFFFFFFF
            crc = (crc >> 1) ^ (0xEDB88320 & mask)
    return (~crc) & 0xFFFFFFFF


def main() -> int:
    ap = argparse.ArgumentParser(description="Parse open-firmware crash dump blob")
    ap.add_argument("path", help="path to crash dump binary")
    args = ap.parse_args()

    with open(args.path, "rb") as f:
        data = f.read()

    if len(data) < CRASH_DUMP_SIZE:
        print(f"error: file too small ({len(data)} bytes), expected {CRASH_DUMP_SIZE}", file=sys.stderr)
        return 2

    magic = be32(data, OFF_MAGIC)
    version = be16(data, OFF_VERSION)
    size = be16(data, OFF_SIZE)
    crc_expected = be32(data, OFF_CRC)

    data_crc = bytearray(data[:CRASH_DUMP_SIZE])
    data_crc[OFF_CRC : OFF_CRC + 4] = b"\x00\x00\x00\x00"
    crc_actual = crc32_compute(data_crc)

    print(f"magic=0x{magic:08X} ({'OK' if magic == CRASH_DUMP_MAGIC else 'BAD'})")
    print(f"version={version} ({'OK' if version == CRASH_DUMP_VERSION else 'BAD'})")
    print(f"size={size} bytes ({'OK' if size == CRASH_DUMP_SIZE else 'BAD'})")
    print(f"crc=0x{crc_expected:08X} ({'OK' if crc_expected == crc_actual else 'BAD'}), computed=0x{crc_actual:08X}")

    print(f"seq={be32(data, OFF_SEQ)} ms={be32(data, OFF_MS)} flags=0x{be32(data, OFF_FLAGS):08X}")
    print(f"sp=0x{be32(data, OFF_SP):08X} lr=0x{be32(data, OFF_LR):08X} pc=0x{be32(data, OFF_PC):08X}")
    print(f"psr=0x{be32(data, OFF_PSR):08X}")
    print(f"cfsr=0x{be32(data, OFF_CFSR):08X} hfsr=0x{be32(data, OFF_HFSR):08X}")
    print(f"dfsr=0x{be32(data, OFF_DFSR):08X} mmfar=0x{be32(data, OFF_MMFAR):08X}")
    print(f"bfar=0x{be32(data, OFF_BFAR):08X} afsr=0x{be32(data, OFF_AFSR):08X}")

    event_count = be16(data, OFF_EVENT_COUNT)
    event_size = be16(data, OFF_EVENT_REC_SIZE)
    event_seq = be32(data, OFF_EVENT_SEQ)
    print(f"events={event_count} size={event_size} seq={event_seq}")

    if event_count and event_size:
        for i in range(min(event_count, CRASH_DUMP_EVENT_MAX)):
            off = OFF_EVENT_RECORDS + (i * event_size)
            raw = data[off : off + event_size]
            print(f"event[{i}]={binascii.hexlify(raw).decode()}")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
