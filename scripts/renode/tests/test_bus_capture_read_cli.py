import os
import sys
import unittest

SCRIPT_DIR = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
if SCRIPT_DIR not in sys.path:
    sys.path.insert(0, SCRIPT_DIR)

import uart_client


class FakeClient:
    def __init__(self) -> None:
        self.calls = []

    def bus_capture_read(self, offset: int, limit: int = 4):
        self.calls.append((offset, limit))
        return [
            uart_client.BusCaptureRecord(dt_ms=10, bus_id=1, data=b"\x01\x02"),
            uart_client.BusCaptureRecord(dt_ms=20, bus_id=2, data=b"\x03"),
        ]

    def close(self) -> None:
        pass


class BusCaptureReadCliTests(unittest.TestCase):
    def test_bus_capture_read_returns_dicts(self) -> None:
        args = uart_client.make_arg_parser().parse_args(
            ["bus-capture-read", "--offset", "5", "--limit", "2"]
        )
        client = FakeClient()

        output = uart_client.run_command(client, args)

        self.assertEqual(client.calls, [(5, 2)])
        self.assertIsInstance(output, list)
        self.assertEqual(output[0]["dt_ms"], 10)
        self.assertEqual(output[0]["bus_id"], 1)
        self.assertEqual(output[0]["data"], "0102")


if __name__ == "__main__":
    unittest.main()
