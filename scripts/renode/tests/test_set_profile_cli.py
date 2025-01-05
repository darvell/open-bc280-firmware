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

    def set_profile(self, profile_id: int, persist: bool = True) -> None:
        self.calls.append((profile_id, persist))

    def debug_state(self) -> uart_client.DebugState:
        return uart_client.DebugState(
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

    def close(self) -> None:
        pass


class SetProfileCliTests(unittest.TestCase):
    def test_set_profile_persists_by_default(self) -> None:
        args = uart_client.make_arg_parser().parse_args(["set-profile", "--id", "2"])
        client = FakeClient()

        output = uart_client.run_command(client, args)

        self.assertEqual(client.calls, [(2, True)])
        self.assertEqual(output[0]["profile_id"], 2)

    def test_set_profile_no_persist(self) -> None:
        args = uart_client.make_arg_parser().parse_args(
            ["set-profile", "--id", "4", "--no-persist"]
        )
        client = FakeClient()

        output = uart_client.run_command(client, args)

        self.assertEqual(client.calls, [(4, False)])
        self.assertEqual(output[0]["profile_id"], 2)


if __name__ == "__main__":
    unittest.main()
