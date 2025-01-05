import dataclasses
import io
import os
import sys
import unittest
from contextlib import redirect_stdout

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
        self.called = False

    def state_dump(self) -> FakeState:
        self.called = True
        return FakeState(
            ms=100,
            rpm=90,
            torque=10,
            speed_dmph=123,
            soc=55,
            err=0,
            last_ms=95,
        )

    def close(self) -> None:
        pass


class StateDumpCliTests(unittest.TestCase):
    def test_state_dump_prints_state(self) -> None:
        args = uart_client.make_arg_parser().parse_args(["state-dump"])
        client = FakeClient()
        buf = io.StringIO()
        expected = dataclasses.asdict(
            FakeState(
                ms=100,
                rpm=90,
                torque=10,
                speed_dmph=123,
                soc=55,
                err=0,
                last_ms=95,
            )
        )

        with redirect_stdout(buf):
            output = uart_client.run_command(client, args)

        self.assertTrue(client.called)
        self.assertIsNone(output)
        self.assertEqual(str(expected), buf.getvalue().strip())


if __name__ == "__main__":
    unittest.main()
