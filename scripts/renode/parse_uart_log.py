#!/usr/bin/env python3
"""Parse BC280 UART logs and decode telemetry frames.

Reads raw bytes captured by the Renode USART1 stub (uart1_tx.log) and extracts
0x55-framed packets. UART1 maps to the BLE module passthrough on real hardware.
By default, emits decoded telemetry stream (0x81) frames.
"""

import argparse
import json
import os
import struct
import sys
from typing import Dict, Iterable, List, Tuple

SOF = 0x55


def checksum(buf: bytes) -> int:
    x = 0
    for b in buf:
        x ^= b
    return (~x) & 0xFF


def iter_frames(data: bytes) -> Iterable[Tuple[int, int, bytes]]:
    i = 0
    n = len(data)
    while i + 4 <= n:
        if data[i] != SOF:
            i += 1
            continue
        if i + 3 > n:
            break
        cmd = data[i + 1]
        length = data[i + 2]
        end = i + 3 + length
        if end >= n:
            break
        want = checksum(data[i:end])
        got = data[end]
        if want == got:
            payload = data[i + 3:end]
            yield i, cmd, payload
            i = end + 1
        else:
            i += 1


def parse_telemetry(payload: bytes) -> Dict[str, int]:
    if len(payload) < 22:
        raise ValueError("telemetry payload too short")
    ver = payload[0]
    size = payload[1]
    if ver != 1 or size != len(payload):
        raise ValueError(f"telemetry version/size mismatch ver={ver} size={size}")
    ms = struct.unpack(">I", payload[2:6])[0]
    speed_dmph = struct.unpack(">H", payload[6:8])[0]
    cadence_rpm = struct.unpack(">H", payload[8:10])[0]
    power_w = struct.unpack(">H", payload[10:12])[0]
    batt_dV = struct.unpack(">h", payload[12:14])[0]
    batt_dA = struct.unpack(">h", payload[14:16])[0]
    ctrl_temp_dC = struct.unpack(">h", payload[16:18])[0]
    assist_mode = payload[18]
    profile_id = payload[19]
    virtual_gear = payload[20]
    flags = payload[21]
    return {
        "version": ver,
        "size": size,
        "ms": ms,
        "speed_dmph": speed_dmph,
        "cadence_rpm": cadence_rpm,
        "power_w": power_w,
        "batt_dV": batt_dV,
        "batt_dA": batt_dA,
        "ctrl_temp_dC": ctrl_temp_dC,
        "assist_mode": assist_mode,
        "profile_id": profile_id,
        "virtual_gear": virtual_gear,
        "flags": flags,
        "brake": 1 if (flags & 0x01) else 0,
        "walk": 1 if (flags & 0x02) else 0,
    }


def default_log_path() -> str:
    outdir = os.environ.get("BC280_RENODE_OUTDIR")
    if not outdir:
        here = os.path.abspath(os.path.dirname(__file__))
        outdir = os.path.abspath(os.path.join(here, "..", "..", "open-firmware", "renode", "output"))
    return os.path.join(outdir, "uart1_tx.log")


def format_text(entry: Dict[str, int]) -> str:
    return (
        f"t={entry['ms']}ms spd={entry['speed_dmph']/10.0:.1f}mph "
        f"cad={entry['cadence_rpm']}rpm pow={entry['power_w']}W "
        f"batt={entry['batt_dV']/10.0:.1f}V {entry['batt_dA']/10.0:.1f}A "
        f"temp={entry['ctrl_temp_dC']/10.0:.1f}C "
        f"assist={entry['assist_mode']} profile={entry['profile_id']} vgear={entry['virtual_gear']} "
        f"flags=0x{entry['flags']:02x} brake={entry['brake']} walk={entry['walk']}"
    )


def run_demo(json_mode: bool) -> int:
    payload = bytearray(
        [
            1,
            22,
            0x00,
            0x00,
            0x04,
            0xD2,  # ms=1234
            0x00,
            0x7B,  # speed_dmph=123
            0x00,
            0x50,  # cadence=80
            0x01,
            0x41,  # power=321
            0x02,
            0x00,  # batt_dV=512
            0xFF,
            0xDD,  # batt_dA=-35
            0x01,
            0xDC,  # temp=476
            2,      # assist_mode
            1,      # profile_id
            3,      # virtual_gear
            0x03,   # flags (brake+walk)
        ]
    )
    frame = bytearray([SOF, 0x81, len(payload)]) + payload
    frame.append(checksum(frame))
    telemetry = parse_telemetry(payload)
    telemetry["offset"] = 0
    telemetry["cmd"] = 0x81
    if json_mode:
        print(json.dumps(telemetry))
    else:
        print(format_text(telemetry))
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description="Parse BC280 UART logs and decode telemetry frames")
    parser.add_argument("--input", "-i", default=default_log_path(), help="path to uart1_tx.log")
    parser.add_argument("--json", action="store_true", help="emit JSON lines")
    parser.add_argument("--all", action="store_true", help="emit non-telemetry frames too")
    parser.add_argument("--telemetry-only", action="store_true", help="only emit telemetry frames (default)")
    parser.add_argument("--limit", type=int, default=0, help="stop after N decoded frames")
    parser.add_argument("--allow-empty", action="store_true", help="exit 0 even if no telemetry frames found")
    parser.add_argument("--demo", action="store_true", help="emit a built-in demo frame and exit")
    args = parser.parse_args()

    if args.demo:
        return run_demo(args.json)

    if not os.path.exists(args.input):
        sys.stderr.write(f"log not found: {args.input}\n")
        return 1

    with open(args.input, "rb") as f:
        data = f.read()

    if args.telemetry_only:
        args.all = False

    count = 0
    decoded = 0
    for offset, cmd, payload in iter_frames(data):
        count += 1
        entry: Dict[str, int] = {"offset": offset, "cmd": cmd, "len": len(payload)}
        is_telem = False
        if cmd == 0x81 and len(payload) >= 2 and payload[0] == 1 and payload[1] == len(payload):
            try:
                entry.update(parse_telemetry(payload))
                is_telem = True
            except ValueError:
                is_telem = False
        if not args.all and not is_telem:
            continue
        if not is_telem:
            entry["payload_hex"] = payload.hex()
        decoded += 1
        if args.json:
            print(json.dumps(entry))
        else:
            if is_telem:
                print(format_text(entry))
            else:
                print(f"offset=0x{offset:x} cmd=0x{cmd:02x} len={len(payload)} payload={payload.hex()}")
        if args.limit and decoded >= args.limit:
            break

    if decoded == 0:
        msg = "no telemetry frames decoded" if not args.all else "no frames decoded"
        sys.stderr.write(msg + "\n")
        return 0 if args.allow_empty else 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
