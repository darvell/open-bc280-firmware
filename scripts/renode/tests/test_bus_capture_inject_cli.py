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

    def bus_capture_inject(self, bus_id: int, dt_ms: int, data: bytes) -> int:
        self.calls.append((bus_id, dt_ms, data))
        return 7

    def close(self) -> None:
        pass


class BusCaptureInjectCliTests(unittest.TestCase):
    def test_bus_capture_inject_returns_seq(self) -> None:
        args = uart_client.make_arg_parser().parse_args(
            ["bus-capture-inject", "--bus-id", "2", "--dt-ms", "15", "--data-hex", "0102aa"]
        )
        client = FakeClient()

        output = uart_client.run_command(client, args)

        self.assertEqual(client.calls, [(2, 15, b"\x01\x02\xaa")])
        self.assertEqual(output, [{"seq": 7}])


if __name__ == "__main__":
    unittest.main()
