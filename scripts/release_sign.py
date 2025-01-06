#!/usr/bin/env python3
import argparse
import os
import struct
import sys

META_MAGIC = 0x4F46574D  # 'OFWM'
META_VERSION = 1
META_SIZE = 32
FLAG_SIGNED = 0x01


def crc32_compute(data: bytes) -> int:
    crc = 0xFFFFFFFF
    for b in data:
        crc ^= b
        for _ in range(8):
            mask = -(crc & 1) & 0xFFFFFFFF
            crc = (crc >> 1) ^ (0xEDB88320 & mask)
    return (~crc) & 0xFFFFFFFF


def be32(value: int) -> bytes:
    return struct.pack(">I", value & 0xFFFFFFFF)


def parse_key(val: str) -> int:
    try:
        return int(val, 0)
    except Exception:
        raise argparse.ArgumentTypeError(f"invalid key '{val}'")


def load_meta(buf: bytes):
    if len(buf) != META_SIZE:
        raise ValueError("metadata size mismatch")
    return struct.unpack("<IHHIIIIII", buf)


def pack_meta(fields) -> bytes:
    return struct.pack("<IHHIIIIII", *fields)


def main() -> int:
    parser = argparse.ArgumentParser(description="Sign or clear open-firmware release metadata")
    parser.add_argument("--in", dest="in_path", required=True, help="input firmware bin (in-place if --out not set)")
    parser.add_argument("--out", dest="out_path", help="optional output path")
    parser.add_argument("--mode", choices=("sign", "unsigned", "invalidate"), default="sign")
    parser.add_argument("--key", type=parse_key, help="signing key (numeric literal, e.g. 0x1A2B3C4D)")
    args = parser.parse_args()

    in_path = args.in_path
    out_path = args.out_path or in_path

    with open(in_path, "rb") as f:
        data = bytearray(f.read())

    if len(data) < META_SIZE:
        sys.stderr.write("error: firmware too small to contain release metadata\n")
        return 2

    meta_off = len(data) - META_SIZE
    meta = data[meta_off:]
    magic, version, size, image_crc, signature, flags, build_id, res0, res1 = load_meta(meta)
    if magic != META_MAGIC or version != META_VERSION or size != META_SIZE:
        sys.stderr.write("error: release metadata missing or version mismatch (rebuild firmware)\n")
        return 2

    image = bytes(data[:meta_off])
    image_size = len(image)
    image_crc_calc = crc32_compute(image)

    key = args.key
    if key is None:
        env_key = os.environ.get("RELEASE_SIGN_KEY")
        if env_key:
            key = parse_key(env_key)

    if args.mode == "unsigned":
        flags &= ~FLAG_SIGNED
        image_crc = 0
        signature = 0
    else:
        if key is None:
            sys.stderr.write("error: signing key required (use --key or RELEASE_SIGN_KEY)\n")
            return 2
        flags |= FLAG_SIGNED
        image_crc = image_crc_calc
        sig_expected = crc32_compute(be32(image_crc_calc) + be32(image_size) + be32(key))
        if args.mode == "invalidate":
            signature = sig_expected ^ 0xFFFFFFFF
        else:
            signature = sig_expected

    new_fields = (
        META_MAGIC,
        META_VERSION,
        META_SIZE,
        image_crc,
        signature,
        flags,
        build_id,
        res0,
        res1,
    )
    data[meta_off:] = pack_meta(new_fields)

    with open(out_path, "wb") as f:
        f.write(data)

    sys.stdout.write(
        f"ok: mode={args.mode} size={image_size} crc=0x{image_crc_calc:08X} out={out_path}\n"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
