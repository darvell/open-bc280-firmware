#!/usr/bin/env python3
"""Renode test for BLE hacker-mode control-plane frames."""

import os
import sys
import time

from uart_client import ProtocolError, UARTClient, TelemetryV1

PORT = os.environ.get("BC280_UART1_PTY", "/tmp/uart1")

OP_VERSION = 0x01
OP_TELEMETRY = 0x02
OP_CONFIG_GET = 0x10
OP_CONFIG_STAGE = 0x11
OP_CONFIG_COMMIT = 0x12
OP_ERROR = 0x7F
RESP_FLAG = 0x80

STATUS_OK = 0x00
STATUS_BAD_VERSION = 0xF0
STATUS_BAD_LENGTH = 0xF1
STATUS_BAD_PAYLOAD = 0xF2
STATUS_BAD_OPCODE = 0xF3
STATUS_NO_STAGED = 0xFD
STATUS_REJECT = 0xFE


def expect(cond: bool, msg: str) -> None:
    if not cond:
        raise AssertionError(msg)


def wait_for_pty(path: str, timeout_s: float = 5.0) -> None:
    deadline = time.time() + timeout_s
    while time.time() < deadline:
        if os.path.exists(path):
            return
        time.sleep(0.1)
    raise FileNotFoundError(f"UART PTY not found at {path}")


def build_frame(op: int, payload: bytes = b"", ver: int = 1) -> bytes:
    if len(payload) > 255:
        raise ValueError("payload too large")
    return bytes([ver & 0xFF, op & 0xFF, len(payload) & 0xFF]) + payload


def parse_frame(data: bytes) -> tuple[int, int, bytes]:
    if len(data) < 3:
        raise AssertionError("frame too short")
    ver, op, plen = data[0], data[1], data[2]
    if len(data) != plen + 3:
        raise AssertionError(f"frame length mismatch {len(data)} != {plen + 3}")
    return ver, op, data[3:]


def parse_response(data: bytes) -> tuple[int, int, int, bytes]:
    ver, op, payload = parse_frame(data)
    if len(payload) < 1:
        raise AssertionError("response missing status")
    return ver, op, payload[0], payload[1:]


def main() -> int:
    try:
        wait_for_pty(PORT)
    except FileNotFoundError as e:
        sys.stderr.write(str(e) + "\n")
        return 1

    client = UARTClient(PORT, baud=115200, timeout=0.5)
    try:
        client.ping()

        # Version handshake.
        resp = client.ble_hacker_exchange(build_frame(OP_VERSION))
        ver, op, status, payload = parse_response(resp)
        expect(ver == 1, "version response ver mismatch")
        expect(op == (OP_VERSION | RESP_FLAG), "version response opcode mismatch")
        expect(status == STATUS_OK, "version response status mismatch")
        expect(len(payload) == 3, "version payload length mismatch")
        max_payload = payload[1]
        caps = payload[2]
        expect(max_payload >= 80, "max payload too small")
        expect(caps & 0x02, "config capability not advertised")

        # Telemetry snapshot.
        client.set_state(
            rpm=120,
            torque=30,
            speed_dmph=123,
            soc=87,
            err=0,
            cadence_rpm=80,
            power_w=250,
            batt_dV=512,
            batt_dA=-10,
            ctrl_temp_dC=400,
        )
        resp = client.ble_hacker_exchange(build_frame(OP_TELEMETRY))
        ver, op, status, payload = parse_response(resp)
        expect(ver == 1, "telemetry response ver mismatch")
        expect(op == (OP_TELEMETRY | RESP_FLAG), "telemetry response opcode mismatch")
        expect(status == STATUS_OK, "telemetry response status mismatch")
        telem = TelemetryV1.from_payload(payload)
        expect(telem.speed_dmph == 123, "telemetry speed mismatch")
        expect(telem.cadence_rpm == 80, "telemetry cadence mismatch")
        expect(telem.power_w == 250, "telemetry power mismatch")

        # Config stage/commit via hacker mode (same path as UART debug).
        active = client.config_get()
        active.wheel_mm = 2150 if active.wheel_mm == 2100 else 2100
        active.seq = active.seq + 1
        cfg_payload = active.to_payload(recalc_crc=True)
        resp = client.ble_hacker_exchange(build_frame(OP_CONFIG_STAGE, cfg_payload))
        _, op, status, _ = parse_response(resp)
        expect(op == (OP_CONFIG_STAGE | RESP_FLAG), "config_stage opcode mismatch")
        expect(status == STATUS_OK, f"config_stage status {status:#x}")
        resp = client.ble_hacker_exchange(build_frame(OP_CONFIG_COMMIT, bytes([0])))
        _, op, status, _ = parse_response(resp)
        expect(op == (OP_CONFIG_COMMIT | RESP_FLAG), "config_commit opcode mismatch")
        expect(status == STATUS_OK, f"config_commit status {status:#x}")
        updated = client.config_get()
        expect(updated.wheel_mm == active.wheel_mm, "config_commit did not persist")

        # Bad CRC should be rejected.
        corrupt = bytearray(cfg_payload)
        corrupt[9] ^= 0xFF
        resp = client.ble_hacker_exchange(build_frame(OP_CONFIG_STAGE, bytes(corrupt)))
        _, op, status, _ = parse_response(resp)
        expect(op == (OP_CONFIG_STAGE | RESP_FLAG), "bad CRC opcode mismatch")
        expect(status == STATUS_REJECT, "bad CRC not rejected")

        # Bad version should report framing error.
        resp = client.ble_hacker_exchange(build_frame(OP_VERSION, b"", ver=2))
        ver, op, status, _ = parse_response(resp)
        expect(ver == 1, "error response ver mismatch")
        expect(op == (OP_ERROR | RESP_FLAG), "error opcode mismatch")
        expect(status == STATUS_BAD_VERSION, "bad version status mismatch")

        # Bad length should report framing error.
        resp = client.ble_hacker_exchange(bytes([1, OP_VERSION, 1]))
        _, op, status, _ = parse_response(resp)
        expect(op == (OP_ERROR | RESP_FLAG), "bad length opcode mismatch")
        expect(status == STATUS_BAD_LENGTH, "bad length status mismatch")

        print("PASS: BLE hacker-mode control-plane frames")
        return 0
    except (AssertionError, ProtocolError) as e:
        sys.stderr.write(f"FAIL: {e}\n")
        return 1
    finally:
        client.close()


if __name__ == "__main__":
    raise SystemExit(main())
