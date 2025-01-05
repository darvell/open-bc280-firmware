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
        self.called = False

    def histogram_trace(self) -> None:
        self.called = True

    def close(self) -> None:
        pass


class HistogramTraceCliTests(unittest.TestCase):
    def test_histogram_trace_calls_client(self) -> None:
        args = uart_client.make_arg_parser().parse_args(["histogram-trace"])
        client = FakeClient()
        buf = io.StringIO()

        with redirect_stdout(buf):
            output = uart_client.run_command(client, args)

        self.assertTrue(client.called)
        self.assertIsNone(output)
        self.assertEqual(buf.getvalue().strip(), "histogram trace emitted")


if __name__ == "__main__":
    unittest.main()
