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
                ms=1,
                type=2,
                flags=3,
                speed_dmph=4,
                batt_dV=5,
                batt_dA=6,
                temp_dC=7,
                cmd_power_w=8,
                cmd_current_dA=9,
            )
        ]

    def close(self) -> None:
        pass


class EventLogReadCliTests(unittest.TestCase):
    def test_event_log_read_returns_dicts(self) -> None:
        args = uart_client.make_arg_parser().parse_args(
            ["event-log-read", "--offset", "3", "--limit", "1"]
        )
        client = FakeClient()

        output = uart_client.run_command(client, args)

        self.assertEqual(client.args, (3, 1))
        self.assertIsInstance(output, list)
        self.assertEqual(output[0]["ms"], 1)
        self.assertEqual(output[0]["type"], 2)


if __name__ == "__main__":
    unittest.main()
