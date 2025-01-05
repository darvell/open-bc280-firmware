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
        return b"\xaa\x55"

    def close(self) -> None:
        pass


class BleHackerExchangeCliTests(unittest.TestCase):
    def test_ble_hacker_exchange_returns_hex(self) -> None:
        args = uart_client.make_arg_parser().parse_args(
            ["ble-hacker-exchange", "--frame-hex", "010203"]
        )
        client = FakeClient()

        output = uart_client.run_command(client, args)

        self.assertEqual(client.calls, [b"\x01\x02\x03"])
        self.assertEqual(output, ["ble hacker exchange: aa55"])


if __name__ == "__main__":
    unittest.main()
