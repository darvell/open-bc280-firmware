import json
import os
import sys
import unittest

SCRIPT_DIR = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
if SCRIPT_DIR not in sys.path:
    sys.path.insert(0, SCRIPT_DIR)

import uart_client


class FakeClient:
    def ring_summary(self) -> uart_client.RingSummary:
        return uart_client.RingSummary(
            count=3,
            capacity=10,
            min=1,
            max=7,
            latest=6,
        )

    def close(self) -> None:
        pass


class UartClientJsonRingSummaryTests(unittest.TestCase):
    def test_ring_summary_json_output(self) -> None:
        parser = uart_client.make_arg_parser()
        parser.add_argument("--json", action="store_true")
        args = parser.parse_args(["--json", "ring-summary"])
        client = FakeClient()

        output = uart_client.run_command(client, args)
        lines = uart_client.format_cli_output(output, json_mode=True)

        self.assertEqual(len(lines), 1)
        payload = json.loads(lines[0])
        self.assertEqual(payload[0]["count"], 3)
        self.assertEqual(payload[0]["latest"], 6)


if __name__ == "__main__":
    unittest.main()
