import dataclasses
import os
import sys
import unittest

SCRIPT_DIR = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
if SCRIPT_DIR not in sys.path:
    sys.path.insert(0, SCRIPT_DIR)

import uart_client


@dataclasses.dataclass
class FakeState:
    ms: int
    rpm: int
    torque: int
    speed_dmph: int
    soc: int
    err: int
    last_ms: int


class FakeClient:
    def __init__(self) -> None:
        self.set_args = None

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
        self.set_args = {
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

    def state_dump(self) -> FakeState:
        return FakeState(
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


class SetStateCliTests(unittest.TestCase):
    def test_set_state_outputs_state(self) -> None:
        args = uart_client.make_arg_parser().parse_args(
            [
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
                "--cadence-rpm",
                "75",
                "--throttle-pct",
                "12",
                "--brake",
                "1",
                "--buttons",
                "3",
                "--power-w",
                "250",
                "--batt-dv",
                "420",
                "--batt-da",
                "-12",
                "--ctrl-temp-dc",
                "365",
            ]
        )
        client = FakeClient()
        expected_args = {
            "rpm": 100,
            "torque": 5,
            "speed_dmph": 220,
            "soc": 80,
            "err": 0,
            "cadence_rpm": 75,
            "throttle_pct": 12,
            "brake": 1,
            "buttons": 3,
            "power_w": 250,
            "batt_dV": 420,
            "batt_dA": -12,
            "ctrl_temp_dC": 365,
        }
        expected_state = dataclasses.asdict(
            FakeState(
                ms=200,
                rpm=110,
                torque=12,
                speed_dmph=234,
                soc=77,
                err=0,
                last_ms=180,
            )
        )

        output = uart_client.run_command(client, args)

        self.assertEqual(output, [expected_state])
        self.assertEqual(expected_args, client.set_args)


if __name__ == "__main__":
    unittest.main()
