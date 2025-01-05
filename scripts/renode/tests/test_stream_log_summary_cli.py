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

    def stream_log_summary(self):
        self.called = True
        return uart_client.StreamLogSummary(
            count=1,
            capacity=2,
            head=3,
            record_size=4,
            period_ms=5,
            enabled=True,
            seq=6,
        )

    def close(self) -> None:
        pass


class StreamLogSummaryCliTests(unittest.TestCase):
    def test_stream_log_summary_returns_dicts(self) -> None:
        args = uart_client.make_arg_parser().parse_args(["stream-log-summary"])
        client = FakeClient()

        output = uart_client.run_command(client, args)

        self.assertTrue(client.called)
        self.assertIsInstance(output, list)
        self.assertEqual(output[0]["count"], 1)
        self.assertEqual(output[0]["enabled"], True)


if __name__ == "__main__":
    unittest.main()
