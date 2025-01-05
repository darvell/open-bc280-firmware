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
        self.called = False

    def stream_log_summary(self) -> uart_client.StreamLogSummary:
        self.called = True
        return uart_client.StreamLogSummary(
            count=4,
            capacity=12,
            head=2,
            record_size=24,
            period_ms=250,
            enabled=True,
            seq=7,
        )

    def close(self) -> None:
        pass


class UartClientJsonStreamLogSummaryTests(unittest.TestCase):
    def test_stream_log_summary_json_output(self) -> None:
        parser = uart_client.make_arg_parser()
        parser.add_argument("--json", action="store_true")
        args = parser.parse_args(["--json", "stream-log-summary"])
        client = FakeClient()

        output = uart_client.run_command(client, args)
        lines = uart_client.format_cli_output(output, json_mode=True)

        self.assertTrue(client.called)
        self.assertEqual(len(lines), 1)
        payload = json.loads(lines[0])
        self.assertEqual(payload[0]["count"], 4)
        self.assertEqual(payload[0]["period_ms"], 250)
        self.assertEqual(payload[0]["enabled"], True)


if __name__ == "__main__":
    unittest.main()
