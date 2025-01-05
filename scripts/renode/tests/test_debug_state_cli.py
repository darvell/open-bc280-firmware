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

    def debug_state(self) -> uart_client.DebugState:
        self.called = True
        return uart_client.DebugState(
            version=1,
            size=64,
            ms=1234,
            inputs_ms=1200,
            speed_dmph=220,
            cadence_rpm=80,
            torque_raw=150,
            throttle_pct=10,
            brake=0,
            buttons=3,
            assist_mode=2,
            profile_id=1,
            virtual_gear=4,
            cmd_power_w=350,
            cmd_current_dA=120,
            cruise_state=0,
        )

    def close(self) -> None:
        pass


class DebugStateCliTests(unittest.TestCase):
    def test_debug_state_returns_dict(self) -> None:
        args = uart_client.make_arg_parser().parse_args(["debug-state"])
        client = FakeClient()

        output = uart_client.run_command(client, args)

        self.assertTrue(client.called)
        self.assertIsInstance(output, list)
        self.assertEqual(output[0]["version"], 1)
        self.assertEqual(output[0]["size"], 64)
        self.assertEqual(output[0]["ms"], 1234)


if __name__ == "__main__":
    unittest.main()
