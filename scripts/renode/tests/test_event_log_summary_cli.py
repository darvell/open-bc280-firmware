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
        return uart_client.EventLogSummary(count=2, capacity=16, head=1, record_size=20, seq=5)

    def close(self) -> None:
        pass


class EventLogSummaryCliTests(unittest.TestCase):
    def test_event_log_summary_returns_dict(self) -> None:
        args = uart_client.make_arg_parser().parse_args(["event-log-summary"])
        client = FakeClient()

        output = uart_client.run_command(client, args)

        self.assertTrue(client.called)
        self.assertIsInstance(output, list)
        self.assertEqual(len(output), 1)
        self.assertEqual(output[0]["count"], 2)
        self.assertEqual(output[0]["capacity"], 16)


if __name__ == "__main__":
    unittest.main()
