import io
import os
import sys
import unittest
from contextlib import redirect_stdout

SCRIPT_DIR = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
if SCRIPT_DIR not in sys.path:
    sys.path.insert(0, SCRIPT_DIR)

import uart_client


class FakeClient:
    def __init__(self) -> None:
        self.calls = []

    def marker_control(self, enable: bool) -> None:
        self.calls.append(enable)

    def close(self) -> None:
        pass


class MarkerControlCliTests(unittest.TestCase):
    def test_marker_control_enable(self) -> None:
        args = uart_client.make_arg_parser().parse_args(["marker-control"])
        client = FakeClient()
        buf = io.StringIO()

        with redirect_stdout(buf):
            output = uart_client.run_command(client, args)

        self.assertEqual(client.calls, [True])
        self.assertIsNone(output)
        self.assertEqual(buf.getvalue().strip(), "markers enabled")

    def test_marker_control_disable(self) -> None:
        args = uart_client.make_arg_parser().parse_args(
            ["marker-control", "--disable"]
        )
        client = FakeClient()
        buf = io.StringIO()

        with redirect_stdout(buf):
            output = uart_client.run_command(client, args)

        self.assertEqual(client.calls, [False])
        self.assertIsNone(output)
        self.assertEqual(buf.getvalue().strip(), "markers disabled")


if __name__ == "__main__":
    unittest.main()
