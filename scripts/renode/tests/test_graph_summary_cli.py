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
    def graph_summary(self) -> uart_client.GraphSummary:
        return uart_client.GraphSummary(
            count=4,
            capacity=12,
            min=2,
            max=8,
            latest=6,
            period_ms=200,
            window_ms=60000,
        )

    def close(self) -> None:
        pass


class GraphSummaryCliTests(unittest.TestCase):
    def test_graph_summary_returns_dicts(self) -> None:
        args = uart_client.make_arg_parser().parse_args(["graph-summary"])
        client = FakeClient()
        buf = io.StringIO()

        with redirect_stdout(buf):
            output = uart_client.run_command(client, args)

        self.assertIsNone(output)
        self.assertEqual(
            buf.getvalue().strip(),
            "{'count': 4, 'capacity': 12, 'min': 2, 'max': 8, 'latest': 6, 'period_ms': 200, 'window_ms': 60000}",
        )


if __name__ == "__main__":
    unittest.main()
