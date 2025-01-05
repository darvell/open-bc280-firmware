#!/usr/bin/env python3
"""Preflight check for BC280 open-firmware images before flashing.

Validates:
- image exists and is non-empty
- image size <= app slot (default 0x30000)
- vector table SP/PC satisfy OEM bootloader checks
- PC (masked) falls inside the image loaded at base (default 0x08010000)
"""
from __future__ import annotations

import argparse
from pathlib import Path


def parse_int(val: str) -> int:
    return int(val, 0)


def main() -> int:
    ap = argparse.ArgumentParser(description="Preflight check for open-firmware image")
    ap.add_argument("--image", default="build/open_firmware.bin", help="Path to .bin")
    ap.add_argument("--base", default="0x08010000", help="Load address for app image")
    ap.add_argument("--max-size", default="0x30000", help="Max bytes for app slot")
    args = ap.parse_args()

    image = Path(args.image)
    base = parse_int(args.base)
    max_size = parse_int(args.max_size)

    if not image.exists():
        print(f"FAIL: image not found: {image}")
        return 1

    data = image.read_bytes()
    size = len(data)
    if size == 0:
        print("FAIL: image is empty")
        return 1

    if size > max_size:
        print(f"FAIL: image too large ({size} > {max_size} bytes)")
        return 1

    if size < 8:
        print("FAIL: image too small for vector table")
        return 1

    sp = int.from_bytes(data[0:4], "little")
    pc = int.from_bytes(data[4:8], "little")
    pc_masked = pc & ~1

    sp_ok = (sp & 0x2FFE0000) == 0x20000000
    pc_ok = (pc & 0xFFF80000) == 0x08000000
    if not sp_ok:
        print(f"FAIL: SP out of range (SP=0x{sp:08X})")
        return 1
    if not pc_ok:
        print(f"FAIL: PC out of range (PC=0x{pc:08X})")
        return 1

    image_start = base
    image_end = base + size
    if not (image_start <= pc_masked < image_end):
        print(
            "FAIL: PC not inside image range "
            f"(PC=0x{pc:08X} masked=0x{pc_masked:08X}, "
            f"range=0x{image_start:08X}-0x{image_end - 1:08X})"
        )
        return 1

    slack = max_size - size
    print("PASS: image preflight OK")
    print(f"  image: {image}")
    print(f"  size: {size} bytes (slack {slack} bytes)")
    print(f"  SP: 0x{sp:08X}")
    print(f"  PC: 0x{pc:08X}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
