#!/usr/bin/env python3
"""Renode regression for optional BLE MITM/mimic tooling.

Exercises state transitions and capture blob formatting without a real BLE radio.
"""

import os
import sys
import time

from uart_client import (
    UARTClient,
    ProtocolError,
    BLE_MITM_EVENT_ADV,
    BLE_MITM_EVENT_CONNECT,
    BLE_MITM_EVENT_DISCONNECT,
    BLE_MITM_EVENT_RX,
    BLE_MITM_EVENT_TX,
    BLE_MITM_MAGIC,
    BLE_MITM_MODE_PERIPHERAL,
    BLE_MITM_STATE_ADVERTISING,
    BLE_MITM_STATE_CONNECTED,
    BLE_MITM_STATUS_OK,
    BLE_MITM_VERSION,
    BLE_MITM_DIR_RX,
    BLE_MITM_DIR_TX,
)

PORT = os.environ.get("BC280_UART1_PTY", "/tmp/uart1")


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


def main() -> int:
    try:
        wait_for_pty(PORT)
    except FileNotFoundError as e:
        sys.stderr.write(str(e) + "\n")
        return 1

    client = UARTClient(PORT, baud=115200, timeout=0.5)
    try:
        client.ping()

        status = client.ble_mitm_status()
        expect(status.version == BLE_MITM_VERSION, "status version mismatch")
        expect(not status.enabled, "MITM should start disabled")

        # Start advertising (peripheral mimic), then connect.
        code = client.ble_mitm_control(True, BLE_MITM_MODE_PERIPHERAL, BLE_MITM_EVENT_ADV)
        expect(code == BLE_MITM_STATUS_OK, f"adv status {code:#x}")
        status = client.ble_mitm_status()
        expect(status.enabled, "MITM should be enabled")
        expect(status.state == BLE_MITM_STATE_ADVERTISING, "state not advertising")

        code = client.ble_mitm_control(True, BLE_MITM_MODE_PERIPHERAL, BLE_MITM_EVENT_CONNECT)
        expect(code == BLE_MITM_STATUS_OK, f"connect status {code:#x}")
        status = client.ble_mitm_status()
        expect(status.state == BLE_MITM_STATE_CONNECTED, "state not connected")

        # Inject RX/TX frames to populate capture.
        rx_payload = b"\x01\x02\x03"
        tx_payload = b"\xAA"
        code = client.ble_mitm_control(True, BLE_MITM_MODE_PERIPHERAL, BLE_MITM_EVENT_RX, rx_payload)
        expect(code == BLE_MITM_STATUS_OK, f"rx status {code:#x}")
        code = client.ble_mitm_control(True, BLE_MITM_MODE_PERIPHERAL, BLE_MITM_EVENT_TX, tx_payload)
        expect(code == BLE_MITM_STATUS_OK, f"tx status {code:#x}")

        # Disconnect back to advertising.
        code = client.ble_mitm_control(True, BLE_MITM_MODE_PERIPHERAL, BLE_MITM_EVENT_DISCONNECT)
        expect(code == BLE_MITM_STATUS_OK, f"disconnect status {code:#x}")
        status = client.ble_mitm_status()
        expect(status.state == BLE_MITM_STATE_ADVERTISING, "state not advertising after disconnect")

        capture = client.ble_mitm_capture_read()
        expect(capture.magic == BLE_MITM_MAGIC, "capture magic mismatch")
        expect(capture.version == BLE_MITM_VERSION, "capture version mismatch")
        expect(capture.record_count >= 2, "expected at least 2 capture records")
        expect(len(capture.records) >= 2, "missing capture records")

        last_two = capture.records[-2:]
        expect(last_two[0].direction == BLE_MITM_DIR_RX, "RX direction mismatch")
        expect(last_two[0].data == rx_payload, "RX payload mismatch")
        expect(last_two[1].direction == BLE_MITM_DIR_TX, "TX direction mismatch")
        expect(last_two[1].data == tx_payload, "TX payload mismatch")

        print("PASS: BLE MITM state + capture blob")
        return 0
    except (AssertionError, ProtocolError) as e:
        sys.stderr.write(f"FAIL: {e}\n")
        return 1
    finally:
        client.close()


if __name__ == "__main__":
    raise SystemExit(main())
