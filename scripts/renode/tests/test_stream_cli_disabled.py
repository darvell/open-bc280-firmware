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
        self.period_ms = None

    def set_stream(self, period_ms: int) -> None:
        self.period_ms = period_ms

    def close(self) -> None:
        pass


class StreamCliDisabledTests(unittest.TestCase):
    def test_stream_disabled_prints_message(self) -> None:
        args = uart_client.make_arg_parser().parse_args(
            ["stream", "--period-ms", "0"]
        )
        client = FakeClient()
        buf = io.StringIO()

        with redirect_stdout(buf):
            output = uart_client.run_command(client, args)

        self.assertEqual(client.period_ms, 0)
        self.assertIsNone(output)
        self.assertEqual(buf.getvalue().strip(), "stream disabled")


if __name__ == "__main__":
    unittest.main()
