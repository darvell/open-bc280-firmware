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
            version=1,
            enabled=True,
            mode=uart_client.BLE_MITM_MODE_CENTRAL,
            state=uart_client.BLE_MITM_STATE_CONNECTED,
            count=2,
            seq=5,
        )

    def close(self) -> None:
        pass


class BleMitmStatusCliTests(unittest.TestCase):
    def test_ble_mitm_status_returns_dict(self) -> None:
        args = uart_client.make_arg_parser().parse_args(["ble-mitm-status"])
        client = FakeClient()

        output = uart_client.run_command(client, args)

        self.assertTrue(client.called)
        self.assertIsInstance(output, list)
        self.assertEqual(output[0]["enabled"], True)
        self.assertEqual(output[0]["mode"], uart_client.BLE_MITM_MODE_CENTRAL)


if __name__ == "__main__":
    unittest.main()
