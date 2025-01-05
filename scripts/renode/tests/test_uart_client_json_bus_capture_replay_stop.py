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

    def bus_capture_replay_stop(self) -> None:
        self.called = True

    def close(self) -> None:
        pass


class UartClientJsonBusCaptureReplayStopTests(unittest.TestCase):
    def test_bus_capture_replay_stop_json_output(self) -> None:
        parser = uart_client.make_arg_parser()
        parser.add_argument("--json", action="store_true")
        args = parser.parse_args(["--json", "bus-capture-replay-stop"])
        client = FakeClient()

        output = uart_client.run_command(client, args)
        lines = uart_client.format_cli_output(output, json_mode=True)

        self.assertTrue(client.called)
        self.assertEqual(lines, [json.dumps(["bus capture replay stopped"])])


if __name__ == "__main__":
    unittest.main()
