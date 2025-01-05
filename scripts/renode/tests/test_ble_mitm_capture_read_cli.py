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
        return uart_client.BleMitmCapture(
            magic=uart_client.BLE_MITM_MAGIC,
            version=1,
            header_size=uart_client.BLE_MITM_HEADER_SIZE,
            record_count=2,
            max_len=uart_client.BLE_MITM_MAX_DATA,
            seq=9,
            flags=0,
            records=[
                uart_client.BleMitmRecord(dt_ms=10, direction=uart_client.BLE_MITM_DIR_RX, data=b"\x01"),
                uart_client.BleMitmRecord(dt_ms=20, direction=uart_client.BLE_MITM_DIR_TX, data=b"\x02\x03"),
            ],
        )

    def close(self) -> None:
        pass


class BleMitmCaptureReadCliTests(unittest.TestCase):
    def test_ble_mitm_capture_read_returns_dicts(self) -> None:
        args = uart_client.make_arg_parser().parse_args(["ble-mitm-capture-read"])
        client = FakeClient()

        output = uart_client.run_command(client, args)

        self.assertTrue(client.called)
        self.assertIsInstance(output, list)
        self.assertEqual(output[0]["record_count"], 2)
        self.assertEqual(output[1]["dt_ms"], 10)
        self.assertEqual(output[1]["data"], "01")
        self.assertEqual(output[2]["direction"], uart_client.BLE_MITM_DIR_TX)
        self.assertEqual(output[2]["data"], "0203")


if __name__ == "__main__":
    unittest.main()
