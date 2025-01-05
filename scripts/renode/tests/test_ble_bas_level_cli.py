import os
import sys
import unittest

SCRIPT_DIR = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
if SCRIPT_DIR not in sys.path:
    sys.path.insert(0, SCRIPT_DIR)

import uart_client


class FakeClient:
    def __init__(self) -> None:
        self.called = False

    def ble_bas_level(self) -> int:
        self.called = True
        return 87

    def close(self) -> None:
        pass


class BleBasLevelCliTests(unittest.TestCase):
    def test_ble_bas_level_returns_value(self) -> None:
        args = uart_client.make_arg_parser().parse_args(["ble-bas-level"])
        client = FakeClient()

        output = uart_client.run_command(client, args)

        self.assertTrue(client.called)
        self.assertEqual(output, ["ble bas level: 87"])


if __name__ == "__main__":
    unittest.main()
