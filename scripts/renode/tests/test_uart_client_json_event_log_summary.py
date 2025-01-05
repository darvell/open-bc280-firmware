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

    def event_log_summary(self) -> uart_client.EventLogSummary:
        self.called = True
        return uart_client.EventLogSummary(
            count=5,
            capacity=16,
            head=3,
            record_size=20,
            seq=9,
        )

    def close(self) -> None:
        pass


class UartClientJsonEventLogSummaryTests(unittest.TestCase):
    def test_event_log_summary_json_output(self) -> None:
        parser = uart_client.make_arg_parser()
        parser.add_argument("--json", action="store_true")
        args = parser.parse_args(["--json", "event-log-summary"])
        client = FakeClient()

        output = uart_client.run_command(client, args)
        lines = uart_client.format_cli_output(output, json_mode=True)

        self.assertTrue(client.called)
        self.assertEqual(len(lines), 1)
        payload = json.loads(lines[0])
        self.assertEqual(payload[0]["count"], 5)
        self.assertEqual(payload[0]["record_size"], 20)


if __name__ == "__main__":
    unittest.main()
