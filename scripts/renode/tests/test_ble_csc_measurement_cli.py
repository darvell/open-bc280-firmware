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

    def ble_csc_measurement(self) -> uart_client.CSCMeasurement:
        self.called = True
        return uart_client.CSCMeasurement(
            flags=1,
            wheel_revs=123,
            wheel_event_time=456,
            crank_revs=7,
            crank_event_time=89,
        )

    def close(self) -> None:
        pass


class BleCscMeasurementCliTests(unittest.TestCase):
    def test_ble_csc_measurement_returns_dict(self) -> None:
        args = uart_client.make_arg_parser().parse_args(["ble-csc-measurement"])
        client = FakeClient()

        output = uart_client.run_command(client, args)

        self.assertTrue(client.called)
        self.assertIsInstance(output, list)
        self.assertEqual(output[0]["wheel_revs"], 123)


if __name__ == "__main__":
    unittest.main()
