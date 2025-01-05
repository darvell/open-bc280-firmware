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

    def ble_mitm_capture_read(self) -> uart_client.BleMitmCapture:
        self.called = True
        records = [
            uart_client.BleMitmRecord(dt_ms=12, direction=1, data=b"\x01\x02"),
            uart_client.BleMitmRecord(dt_ms=34, direction=0, data=b"\xaa\xbb"),
        ]
        return uart_client.BleMitmCapture(
            magic=0x424c4558,
            version=1,
            header_size=uart_client.BLE_MITM_HEADER_SIZE,
            record_count=2,
            max_len=12,
            seq=9,
            flags=0,
            records=records,
        )

    def close(self) -> None:
        pass


class UartClientJsonBleMitmCaptureReadTests(unittest.TestCase):
    def test_ble_mitm_capture_read_json_output(self) -> None:
        parser = uart_client.make_arg_parser()
        parser.add_argument("--json", action="store_true")
        args = parser.parse_args(["--json", "ble-mitm-capture-read"])
        client = FakeClient()

        output = uart_client.run_command(client, args)
        lines = uart_client.format_cli_output(output, json_mode=True)

        self.assertTrue(client.called)
        self.assertEqual(len(lines), 1)
        payload = json.loads(lines[0])
        self.assertEqual(payload[0]["record_count"], 2)
        self.assertEqual(payload[0]["records"][0]["data"], "0102")
        self.assertEqual(payload[1]["data"], "0102")
        self.assertEqual(payload[2]["data"], "aabb")


if __name__ == "__main__":
    unittest.main()
