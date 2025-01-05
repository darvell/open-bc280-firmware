import json
import os
import sys
import unittest

SCRIPT_DIR = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
if SCRIPT_DIR not in sys.path:
    sys.path.insert(0, SCRIPT_DIR)

import uart_client


class FakeClient:
    def __init__(self, payload: bytes) -> None:
        self.payload = payload

    def log_frame(self) -> bytes:
        return self.payload

    def close(self) -> None:
        pass


class UartClientJsonLogTests(unittest.TestCase):
    def test_log_json_output(self) -> None:
        parser = uart_client.make_arg_parser()
        parser.add_argument("--json", action="store_true")
        args = parser.parse_args(["--json", "log"])
        client = FakeClient(b"\x10\x02\xaa\xbb")

        output = uart_client.run_command(client, args)
        lines = uart_client.format_cli_output(output, json_mode=True)

        self.assertEqual(len(lines), 1)
        payload = json.loads(lines[0])
        self.assertEqual(payload, ["code=0x10 len=2 data=aabb"])


if __name__ == "__main__":
    unittest.main()
