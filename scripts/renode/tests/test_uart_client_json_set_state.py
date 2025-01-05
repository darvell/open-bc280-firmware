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

    def set_state(
        self,
        rpm: int,
        torque: int,
        speed_dmph: int,
        soc: int,
        err: int,
        cadence_rpm: int | None = None,
        throttle_pct: int | None = None,
        brake: int | None = None,
        buttons: int | None = None,
        power_w: int | None = None,
        batt_dV: int | None = None,
        batt_dA: int | None = None,
        ctrl_temp_dC: int | None = None,
    ) -> None:
        self.calls.append(
            {
                "rpm": rpm,
                "torque": torque,
                "speed_dmph": speed_dmph,
                "soc": soc,
                "err": err,
                "cadence_rpm": cadence_rpm,
                "throttle_pct": throttle_pct,
                "brake": brake,
                "buttons": buttons,
                "power_w": power_w,
                "batt_dV": batt_dV,
                "batt_dA": batt_dA,
                "ctrl_temp_dC": ctrl_temp_dC,
            }
        )

    def state_dump(self) -> uart_client.State:
        return uart_client.State(
            ms=200,
            rpm=110,
            torque=12,
            speed_dmph=234,
            soc=77,
            err=0,
            last_ms=180,
        )

    def close(self) -> None:
        pass


class UartClientJsonSetStateTests(unittest.TestCase):
    def test_set_state_json_output(self) -> None:
        parser = uart_client.make_arg_parser()
        parser.add_argument("--json", action="store_true")
        args = parser.parse_args(
            [
                "--json",
                "set-state",
                "--rpm",
                "100",
                "--torque",
                "5",
                "--speed-dmph",
                "220",
                "--soc",
                "80",
                "--err",
                "0",
            ]
        )
        client = FakeClient()

        output = uart_client.run_command(client, args)
        lines = uart_client.format_cli_output(output, json_mode=True)

        self.assertEqual(len(client.calls), 1)
        self.assertEqual(len(lines), 1)
        payload = json.loads(lines[0])
        self.assertEqual(payload[0]["rpm"], 110)


if __name__ == "__main__":
    unittest.main()
