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

    def bus_capture_replay_stop(self) -> None:
        self.called = True

    def close(self) -> None:
        pass


class BusCaptureReplayStopCliTests(unittest.TestCase):
    def test_bus_capture_replay_stop(self) -> None:
        args = uart_client.make_arg_parser().parse_args(["bus-capture-replay-stop"])
        client = FakeClient()

        output = uart_client.run_command(client, args)

        self.assertTrue(client.called)
        self.assertEqual(output, ["bus capture replay stopped"])


if __name__ == "__main__":
    unittest.main()
