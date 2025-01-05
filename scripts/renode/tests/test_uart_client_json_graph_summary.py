import json
import os
import sys
import unittest

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


class UartClientJsonGraphSummaryTests(unittest.TestCase):
    def test_graph_summary_json_output(self) -> None:
        parser = uart_client.make_arg_parser()
        parser.add_argument("--json", action="store_true")
        args = parser.parse_args(["--json", "graph-summary"])
        client = FakeClient()

        output = uart_client.run_command(client, args)
        lines = uart_client.format_cli_output(output, json_mode=True)

        self.assertEqual(len(lines), 1)
        payload = json.loads(lines[0])
        self.assertEqual(payload[0]["count"], 4)
        self.assertEqual(payload[0]["window_ms"], 60000)


if __name__ == "__main__":
    unittest.main()
