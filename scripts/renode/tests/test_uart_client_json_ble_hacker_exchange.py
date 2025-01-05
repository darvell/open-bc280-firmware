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

    def ble_hacker_exchange(self, frame: bytes) -> bytes:
        self.calls.append(frame)
        return b"\x0a\x0b\x0c"

    def close(self) -> None:
        pass


class UartClientJsonBleHackerExchangeTests(unittest.TestCase):
    def test_ble_hacker_exchange_json_output(self) -> None:
        parser = uart_client.make_arg_parser()
        parser.add_argument("--json", action="store_true")
        args = parser.parse_args([
            "--json",
            "ble-hacker-exchange",
            "--frame-hex",
            "010203",
        ])
        client = FakeClient()

        output = uart_client.run_command(client, args)
        lines = uart_client.format_cli_output(output, json_mode=True)

        self.assertEqual(client.calls, [b"\x01\x02\x03"])
        self.assertEqual(len(lines), 1)
        payload = json.loads(lines[0])
        self.assertEqual(payload, ["ble hacker exchange: 0a0b0c"])


if __name__ == "__main__":
    unittest.main()
