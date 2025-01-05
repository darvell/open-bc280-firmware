import dataclasses
import json
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


class UartClientJsonStateDumpTests(unittest.TestCase):
    def test_state_dump_json_output(self) -> None:
        parser = uart_client.make_arg_parser()
        parser.add_argument("--json", action="store_true")
        args = parser.parse_args(["--json", "state-dump"])
        client = FakeClient()

        output = uart_client.run_command(client, args)
        lines = uart_client.format_cli_output(output, json_mode=True)

        self.assertTrue(client.called)
        self.assertEqual(len(lines), 1)
        payload = json.loads(lines[0])
        self.assertEqual(payload[0]["rpm"], 90)
        self.assertEqual(payload[0]["last_ms"], 95)


if __name__ == "__main__":
    unittest.main()
