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
        self.args = None

    def event_log_read(self, offset: int, limit: int):
        self.args = (offset, limit)
        return [
            uart_client.EventRecord(
                ms=42,
                type=7,
                flags=1,
                speed_dmph=12,
                batt_dV=340,
                batt_dA=25,
                temp_dC=18,
                cmd_power_w=250,
                cmd_current_dA=15,
            )
        ]

    def close(self) -> None:
        pass


class UartClientJsonEventLogReadTests(unittest.TestCase):
    def test_event_log_read_json_output(self) -> None:
        parser = uart_client.make_arg_parser()
        parser.add_argument("--json", action="store_true")
        args = parser.parse_args(["--json", "event-log-read", "--offset", "2", "--limit", "1"])
        client = FakeClient()

        output = uart_client.run_command(client, args)
        lines = uart_client.format_cli_output(output, json_mode=True)

        self.assertEqual(client.args, (2, 1))
        self.assertEqual(len(lines), 1)
        payload = json.loads(lines[0])
        self.assertEqual(payload[0]["ms"], 42)
        self.assertEqual(payload[0]["cmd_power_w"], 250)


if __name__ == "__main__":
    unittest.main()
