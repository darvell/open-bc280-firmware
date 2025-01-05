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

    def bus_capture_summary(self) -> uart_client.BusCaptureSummary:
        self.called = True
        return uart_client.BusCaptureSummary(
            version=1,
            size=16,
            count=3,
            capacity=32,
            head=2,
            max_len=8,
            enabled=True,
            seq=4,
        )

    def close(self) -> None:
        pass


class BusCaptureSummaryCliTests(unittest.TestCase):
    def test_bus_capture_summary_returns_dict(self) -> None:
        args = uart_client.make_arg_parser().parse_args(["bus-capture-summary"])
        client = FakeClient()

        output = uart_client.run_command(client, args)

        self.assertTrue(client.called)
        self.assertIsInstance(output, list)
        self.assertEqual(output[0]["count"], 3)
        self.assertEqual(output[0]["enabled"], True)


if __name__ == "__main__":
    unittest.main()
