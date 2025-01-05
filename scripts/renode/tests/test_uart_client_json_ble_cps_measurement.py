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

    def ble_cps_measurement(self) -> uart_client.CPSMeasurement:
        self.called = True
        return uart_client.CPSMeasurement(
            flags=0x0c,
            instant_power=250,
            wheel_revs=123,
            wheel_event_time=77,
            crank_revs=9,
            crank_event_time=33,
        )

    def close(self) -> None:
        pass


class UartClientJsonBleCpsMeasurementTests(unittest.TestCase):
    def test_ble_cps_measurement_json_output(self) -> None:
        parser = uart_client.make_arg_parser()
        parser.add_argument("--json", action="store_true")
        args = parser.parse_args(["--json", "ble-cps-measurement"])
        client = FakeClient()

        output = uart_client.run_command(client, args)
        lines = uart_client.format_cli_output(output, json_mode=True)

        self.assertTrue(client.called)
        self.assertEqual(len(lines), 1)
        payload = json.loads(lines[0])
        self.assertEqual(payload[0]["flags"], 0x0c)
        self.assertEqual(payload[0]["instant_power"], 250)
        self.assertEqual(payload[0]["crank_event_time"], 33)


if __name__ == "__main__":
    unittest.main()
