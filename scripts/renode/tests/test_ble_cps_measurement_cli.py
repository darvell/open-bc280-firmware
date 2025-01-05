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

    def ble_cps_measurement(self) -> uart_client.CPSMeasurement:
        self.called = True
        return uart_client.CPSMeasurement(
            flags=1,
            instant_power=250,
            wheel_revs=10,
            wheel_event_time=20,
            crank_revs=5,
            crank_event_time=30,
        )

    def close(self) -> None:
        pass


class BleCpsMeasurementCliTests(unittest.TestCase):
    def test_ble_cps_measurement_returns_dict(self) -> None:
        args = uart_client.make_arg_parser().parse_args(["ble-cps-measurement"])
        client = FakeClient()

        output = uart_client.run_command(client, args)

        self.assertTrue(client.called)
        self.assertIsInstance(output, list)
        self.assertEqual(output[0]["instant_power"], 250)


if __name__ == "__main__":
    unittest.main()
