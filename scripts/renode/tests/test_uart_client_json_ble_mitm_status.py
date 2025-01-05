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
        self.called = False

    def ble_mitm_status(self) -> uart_client.BleMitmStatus:
        self.called = True
        return uart_client.BleMitmStatus(
            version=2,
            enabled=True,
            mode=1,
            state=3,
            count=7,
            seq=11,
        )

    def close(self) -> None:
        pass


class UartClientJsonBleMitmStatusTests(unittest.TestCase):
    def test_ble_mitm_status_json_output(self) -> None:
        parser = uart_client.make_arg_parser()
        parser.add_argument("--json", action="store_true")
        args = parser.parse_args(["--json", "ble-mitm-status"])
        client = FakeClient()

        output = uart_client.run_command(client, args)
        lines = uart_client.format_cli_output(output, json_mode=True)

        self.assertTrue(client.called)
        self.assertEqual(len(lines), 1)
        payload = json.loads(lines[0])
        self.assertEqual(payload[0]["version"], 2)
        self.assertEqual(payload[0]["enabled"], True)
        self.assertEqual(payload[0]["count"], 7)


if __name__ == "__main__":
    unittest.main()
