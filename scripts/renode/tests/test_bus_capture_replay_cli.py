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

    def bus_capture_replay(self, offset: int, rate_ms: int) -> int:
        self.calls.append((offset, rate_ms))
        return 12

    def close(self) -> None:
        pass


class BusCaptureReplayCliTests(unittest.TestCase):
    def test_bus_capture_replay_returns_seq(self) -> None:
        args = uart_client.make_arg_parser().parse_args(
            ["bus-capture-replay", "--offset", "3", "--rate-ms", "50"]
        )
        client = FakeClient()

        output = uart_client.run_command(client, args)

        self.assertEqual(client.calls, [(3, 50)])
        self.assertEqual(output, [{"seq": 12}])


if __name__ == "__main__":
    unittest.main()
