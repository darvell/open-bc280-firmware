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
            ms=0,
            inputs_ms=0,
            speed_dmph=0,
            cadence_rpm=0,
            torque_raw=0,
            throttle_pct=0,
            brake=0,
            buttons=0,
            assist_mode=0,
            profile_id=0,
            virtual_gear=0,
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


class SetGearsCliTests(unittest.TestCase):
    def test_set_gears_calls_client_and_prints_state(self) -> None:
        args = uart_client.make_arg_parser().parse_args(
            [
                "set-gears",
                "--count",
                "3",
                "--shape",
                "1",
                "--min-q15",
                "200",
                "--max-q15",
                "400",
                "--scales",
                "100,200,300",
            ]
        )
        client = FakeClient()
        expected_state = uart_client.dataclasses.asdict(client.state)

        output = uart_client.run_command(client, args)

        self.assertEqual(client.calls, [(3, 1, 200, 400, [100, 200, 300])])
        self.assertEqual(output, [expected_state])


if __name__ == "__main__":
    unittest.main()
