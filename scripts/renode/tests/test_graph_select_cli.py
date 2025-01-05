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
        self.channel = None
        self.window = None
        self.reset = None

    def graph_select(self, channel: int, window: int, reset: bool = False) -> None:
        self.channel = channel
        self.window = window
        self.reset = reset

    def graph_summary(self) -> uart_client.GraphSummary:
        return uart_client.GraphSummary(
            count=2,
            capacity=16,
            min=1,
            max=9,
            latest=5,
            period_ms=250,
            window_ms=120000,
        )

    def close(self) -> None:
        pass


class GraphSelectCliTests(unittest.TestCase):
    def test_graph_select_prints_summary(self) -> None:
        args = uart_client.make_arg_parser().parse_args(
            ["graph-select", "--channel", "power", "--window", "2m", "--reset"]
        )
        client = FakeClient()
        buf = io.StringIO()

        with redirect_stdout(buf):
            output = uart_client.run_command(client, args)

        self.assertEqual(client.channel, uart_client.GRAPH_CHANNELS["power"])
        self.assertEqual(client.window, uart_client.GRAPH_WINDOWS["2m"])
        self.assertTrue(client.reset)
        self.assertIsNone(output)
        self.assertEqual(
            buf.getvalue().strip(),
            "{'count': 2, 'capacity': 16, 'min': 1, 'max': 9, 'latest': 5, 'period_ms': 250, 'window_ms': 120000}",
        )


if __name__ == "__main__":
    unittest.main()
