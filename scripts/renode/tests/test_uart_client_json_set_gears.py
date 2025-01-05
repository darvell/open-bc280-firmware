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
        self.calls = []
        self.state = uart_client.DebugState(
            version=1,
            size=64,
            ms=100,
            inputs_ms=100,
            speed_dmph=0,
            cadence_rpm=0,
            torque_raw=0,
            throttle_pct=0,
            brake=0,
            buttons=0,
            assist_mode=1,
            profile_id=2,
            virtual_gear=3,
            cmd_power_w=0,
            cmd_current_dA=0,
            cruise_state=0,
        )

    def set_gears(
        self,
        count: int,
        shape: int,
        min_q15: int,
        max_q15: int,
        scales: list[int] | None = None,
    ) -> None:
        self.calls.append((count, shape, min_q15, max_q15, scales))

    def debug_state(self) -> uart_client.DebugState:
        return self.state

    def close(self) -> None:
        pass


class UartClientJsonSetGearsTests(unittest.TestCase):
    def test_set_gears_json_output(self) -> None:
        parser = uart_client.make_arg_parser()
        parser.add_argument("--json", action="store_true")
        args = parser.parse_args(
            [
                "--json",
                "set-gears",
                "--count",
                "2",
                "--shape",
                "1",
                "--min-q15",
                "100",
                "--max-q15",
                "200",
            ]
        )
        client = FakeClient()

        output = uart_client.run_command(client, args)
        lines = uart_client.format_cli_output(output, json_mode=True)

        self.assertEqual(client.calls, [(2, 1, 100, 200, None)])
        self.assertEqual(len(lines), 1)
        payload = json.loads(lines[0])
        self.assertEqual(payload[0]["profile_id"], 2)


if __name__ == "__main__":
    unittest.main()
