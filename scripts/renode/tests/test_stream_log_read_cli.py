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
                dt_ms=3,
                speed_dmph=4,
                cadence_rpm=5,
                power_w=6,
                batt_dV=7,
                batt_dA=8,
                temp_dC=9,
                assist_mode=10,
                profile_id=11,
                crc16=12,
            )
        ]

    def close(self) -> None:
        pass


class StreamLogReadCliTests(unittest.TestCase):
    def test_stream_log_read_returns_dicts(self) -> None:
        args = uart_client.make_arg_parser().parse_args(
            ["stream-log-read", "--offset", "5", "--limit", "2"]
        )
        client = FakeClient()

        output = uart_client.run_command(client, args)

        self.assertEqual(client.args, (5, 2))
        self.assertIsInstance(output, list)
        self.assertEqual(output[0]["version"], 1)
        self.assertEqual(output[0]["assist_mode"], 10)


if __name__ == "__main__":
    unittest.main()
