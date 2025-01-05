import json
import os
import sys
import unittest

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


class UartClientJsonGraphSelectTests(unittest.TestCase):
    def test_graph_select_json_output(self) -> None:
        parser = uart_client.make_arg_parser()
        parser.add_argument("--json", action="store_true")
        args = parser.parse_args(
            [
                "--json",
                "graph-select",
                "--channel",
                "power",
                "--window",
                "2m",
                "--reset",
            ]
        )
        client = FakeClient()

        output = uart_client.run_command(client, args)
        lines = uart_client.format_cli_output(output, json_mode=True)

        self.assertEqual(client.channel, uart_client.GRAPH_CHANNELS["power"])
        self.assertEqual(client.window, uart_client.GRAPH_WINDOWS["2m"])
        self.assertTrue(client.reset)
        self.assertEqual(len(lines), 1)
        payload = json.loads(lines[0])
        self.assertEqual(payload[0]["count"], 2)
        self.assertEqual(payload[0]["window_ms"], 120000)


if __name__ == "__main__":
    unittest.main()
