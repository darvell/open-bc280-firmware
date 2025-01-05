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

    def bus_capture_control(self, enable: bool, reset: bool = False) -> None:
        self.calls.append((enable, reset))

    def close(self) -> None:
        pass


class BusCaptureControlCliTests(unittest.TestCase):
    def test_bus_capture_control_enable(self) -> None:
        args = uart_client.make_arg_parser().parse_args(["bus-capture-control"])
        client = FakeClient()

        output = uart_client.run_command(client, args)

        self.assertEqual(client.calls, [(True, False)])
        self.assertEqual(output, ["bus capture enabled"])

    def test_bus_capture_control_disable_with_reset(self) -> None:
        args = uart_client.make_arg_parser().parse_args(
            ["bus-capture-control", "--disable", "--reset"]
        )
        client = FakeClient()

        output = uart_client.run_command(client, args)

        self.assertEqual(client.calls, [(False, True)])
        self.assertEqual(output, ["bus capture disabled"])


if __name__ == "__main__":
    unittest.main()
