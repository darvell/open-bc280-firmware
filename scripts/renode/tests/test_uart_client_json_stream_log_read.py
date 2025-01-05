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

    def stream_log_read(self, offset: int, limit: int):
        self.args = (offset, limit)
        return [
            uart_client.StreamLogRecord(
                version=1,
                flags=2,
                dt_ms=250,
                speed_dmph=15,
                cadence_rpm=90,
                power_w=300,
                batt_dV=360,
                batt_dA=20,
                temp_dC=22,
                assist_mode=3,
                profile_id=1,
                crc16=0x1234,
            )
        ]

    def close(self) -> None:
        pass


class UartClientJsonStreamLogReadTests(unittest.TestCase):
    def test_stream_log_read_json_output(self) -> None:
        parser = uart_client.make_arg_parser()
        parser.add_argument("--json", action="store_true")
        args = parser.parse_args(
            ["--json", "stream-log-read", "--offset", "5", "--limit", "1"]
        )
        client = FakeClient()

        output = uart_client.run_command(client, args)
        lines = uart_client.format_cli_output(output, json_mode=True)

        self.assertEqual(client.args, (5, 1))
        self.assertEqual(len(lines), 1)
        payload = json.loads(lines[0])
        self.assertEqual(payload[0]["version"], 1)
        self.assertEqual(payload[0]["assist_mode"], 3)


if __name__ == "__main__":
    unittest.main()
