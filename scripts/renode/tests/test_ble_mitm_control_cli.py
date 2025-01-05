import os
import sys
import unittest

SCRIPT_DIR = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
if SCRIPT_DIR not in sys.path:
    sys.path.insert(0, SCRIPT_DIR)

import uart_client


class FakeClient:
    def __init__(self) -> None:
        self.calls = []

    def ble_mitm_control(self, enable: bool, mode: int, data: bytes | None = None) -> None:
        self.calls.append((enable, mode, data))

    def close(self) -> None:
        pass


class BleMitmControlCliTests(unittest.TestCase):
    def test_ble_mitm_control_enable(self) -> None:
        args = uart_client.make_arg_parser().parse_args(
            ["ble-mitm-control", "--mode", "peripheral"]
        )
        client = FakeClient()

        output = uart_client.run_command(client, args)

        self.assertEqual(client.calls, [(True, uart_client.BLE_MITM_MODE_PERIPHERAL, None)])
        self.assertEqual(output, ["ble mitm enabled"])

    def test_ble_mitm_control_disable(self) -> None:
        args = uart_client.make_arg_parser().parse_args(
            ["ble-mitm-control", "--disable", "--mode", "central"]
        )
        client = FakeClient()

        output = uart_client.run_command(client, args)

        self.assertEqual(client.calls, [(False, uart_client.BLE_MITM_MODE_CENTRAL, None)])
        self.assertEqual(output, ["ble mitm disabled"])

    def test_ble_mitm_control_data_hex(self) -> None:
        args = uart_client.make_arg_parser().parse_args(
            ["ble-mitm-control", "--mode", "central", "--data-hex", "01ff"]
        )
        client = FakeClient()

        output = uart_client.run_command(client, args)

        self.assertEqual(client.calls, [(True, uart_client.BLE_MITM_MODE_CENTRAL, b"\x01\xff")])
        self.assertEqual(output, ["ble mitm enabled"])


if __name__ == "__main__":
    unittest.main()
