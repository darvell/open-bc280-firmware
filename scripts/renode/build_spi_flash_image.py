#!/usr/bin/env python3
"""Build a minimal OEM-like SPI flash image for BC280 V2.2.8."""
import os
import struct
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
OUT_PATH = Path(os.environ.get("BC280_SPI_FLASH_OUT", REPO_ROOT / "open-firmware" / "renode" / "flash" / "spi_flash_v2_2_8.bin")).resolve()
SIZE = 0x400000  # 4 MiB

# Offsets derived from IDA disassembly of sub_801B0B8 / sub_801ABD8 (V2.2.8)
# within the 0x3FB000 0xD0-byte config block.
OFFSETS = {
    "n750": 0x1C,
    "n160": 0x2E,
    "n260": 0x30,
    "n1276": 0x32,
    "n6": 0x35,
    "n7200": 0x38,
    "n40": 0x3A,
    "byte_20001E05": 0x65,
    "level": 0x6C,
    "n10": 0x6D,
    "n69300": 0x78,
    "n320": 0x7C,
    "n48": 0x80,
}

DEFAULTS = {
    "n750": 750,
    "n160": 290,
    "n260": 260,
    "n1276": 2355,
    "n6": 6,
    "n7200": 7200,
    "n40": 40,
    "byte_20001E05": 1,
    "level": 5,
    "n10": 10,
    "n69300": 69300,
    "n320": 320,
    "n48": 48,
}


def write_u8(buf: bytearray, off: int, val: int) -> None:
    buf[off] = val & 0xFF


def write_u16(buf: bytearray, off: int, val: int) -> None:
    buf[off:off + 2] = struct.pack("<H", val & 0xFFFF)


def write_u32(buf: bytearray, off: int, val: int) -> None:
    buf[off:off + 4] = struct.pack("<I", val & 0xFFFFFFFF)


def main() -> None:
    flash = bytearray([0xFF]) * SIZE

    # Bootloader tag at 0x3FF040
    bl_tag = b"B_JH_FW_BL_DT_BC280_V3.3.6"
    flash[0x3FF040] = len(bl_tag)
    flash[0x3FF041:0x3FF041 + len(bl_tag)] = bl_tag

    # App tag at 0x3FF060
    app_tag = b"B_JH_FW_APP_DT_BC280_V2.2.8"
    flash[0x3FF060] = len(app_tag)
    flash[0x3FF061:0x3FF061 + len(app_tag)] = app_tag

    # Bootloader flag (default: not set)
    flash[0x3FF080:0x3FF084] = b"\xFF\xFF\xFF\xFF"

    # Build the 0xD0 config block with defaults.
    cfg = bytearray([0x00]) * 0xD0
    write_u16(cfg, OFFSETS["n750"], DEFAULTS["n750"])
    write_u16(cfg, OFFSETS["n160"], DEFAULTS["n160"])
    write_u16(cfg, OFFSETS["n260"], DEFAULTS["n260"])
    write_u16(cfg, OFFSETS["n1276"], DEFAULTS["n1276"])
    write_u8(cfg, OFFSETS["n6"], DEFAULTS["n6"])
    write_u16(cfg, OFFSETS["n7200"], DEFAULTS["n7200"])
    write_u8(cfg, OFFSETS["n40"], DEFAULTS["n40"])
    write_u8(cfg, OFFSETS["byte_20001E05"], DEFAULTS["byte_20001E05"])
    write_u8(cfg, OFFSETS["level"], DEFAULTS["level"])
    write_u8(cfg, OFFSETS["n10"], DEFAULTS["n10"])
    write_u32(cfg, OFFSETS["n69300"], DEFAULTS["n69300"])
    write_u16(cfg, OFFSETS["n320"], DEFAULTS["n320"])
    write_u8(cfg, OFFSETS["n48"], DEFAULTS["n48"])

    # Mirror config into both regions used by OEM app.
    flash[0x3FB000:0x3FB000 + len(cfg)] = cfg
    flash[0x3FD000:0x3FD000 + len(cfg)] = cfg

    OUT_PATH.parent.mkdir(parents=True, exist_ok=True)
    OUT_PATH.write_bytes(flash)
    print(f"Wrote {OUT_PATH} ({SIZE} bytes)")


if __name__ == "__main__":
    main()
