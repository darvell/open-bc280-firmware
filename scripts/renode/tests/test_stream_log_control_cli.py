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

    def stream_log_control(self, enable: bool, period_ms: int | None = None) -> None:
        self.calls.append((enable, period_ms))

    def close(self) -> None:
        pass


class StreamLogControlCliTests(unittest.TestCase):
    def test_stream_log_control_enable(self) -> None:
        args = uart_client.make_arg_parser().parse_args(
            ["stream-log-control", "--period-ms", "250"]
        )
        client = FakeClient()

        output = uart_client.run_command(client, args)

        self.assertEqual(client.calls, [(True, 250)])
        self.assertEqual(output, ["stream log enabled"])

    def test_stream_log_control_disable(self) -> None:
        args = uart_client.make_arg_parser().parse_args(
            ["stream-log-control", "--disable"]
        )
        client = FakeClient()

        output = uart_client.run_command(client, args)

        self.assertEqual(client.calls, [(False, None)])
        self.assertEqual(output, ["stream log disabled"])


if __name__ == "__main__":
    unittest.main()
