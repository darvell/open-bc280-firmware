import json
import os
import sys
import unittest

SCRIPT_DIR = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
if SCRIPT_DIR not in sys.path:
    sys.path.insert(0, SCRIPT_DIR)

import uart_client


class UartClientJsonOutputTests(unittest.TestCase):
    def test_format_output_json_encodes_bytes(self) -> None:
        output = [
            {"data": b"\x01\x02", "nested": {"raw": b"\xaa"}},
            "ok",
        ]

        lines = uart_client.format_cli_output(output, json_mode=True)

        self.assertEqual(len(lines), 1)
        payload = json.loads(lines[0])
        self.assertEqual(payload[0]["data"], "0102")
        self.assertEqual(payload[0]["nested"]["raw"], "aa")
        self.assertEqual(payload[1], "ok")

    def test_format_output_text_matches_default(self) -> None:
        output = [
            {"foo": 1},
            "bar",
        ]

        lines = uart_client.format_cli_output(output, json_mode=False)

        self.assertEqual(lines, ["{'foo': 1}", "bar"])


if __name__ == "__main__":
    unittest.main()
