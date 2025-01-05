import io
import os
import struct
import sys
import unittest
from contextlib import redirect_stdout
from unittest.mock import patch

SCRIPT_DIR = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
if SCRIPT_DIR not in sys.path:
    sys.path.insert(0, SCRIPT_DIR)

import uart_client


def make_telemetry_payload(
    *,
    ms: int,
    speed_dmph: int,
    cadence_rpm: int,
    power_w: int,
    batt_dv: int,
    batt_da: int,
    ctrl_temp_dc: int,
    assist_mode: int,
    profile_id: int,
    virtual_gear: int,
    flags: int,
) -> bytes:
    body = struct.pack(
        ">IHHHhhhBBBB",
        ms,
        speed_dmph,
        cadence_rpm,
        power_w,
        batt_dv,
        batt_da,
        ctrl_temp_dc,
        assist_mode,
        profile_id,
        virtual_gear,
        flags,
    )
    return bytes([1, 22]) + body


class FakeClient:
    def __init__(self, payload: bytes) -> None:
        self.payload = payload
        self.period_ms = None
        self.frames_read = 0

    def set_stream(self, period_ms: int) -> None:
        self.period_ms = period_ms

    def read_stream_frame(self, timeout_ms: int = 0) -> bytes:
        self.frames_read += 1
        return self.payload

    def close(self) -> None:
        pass


class StreamCliCaptureTests(unittest.TestCase):
    def test_stream_prints_first_frame_summary(self) -> None:
        payload = make_telemetry_payload(
            ms=123,
            speed_dmph=456,
            cadence_rpm=78,
            power_w=250,
            batt_dv=480,
            batt_da=35,
            ctrl_temp_dc=250,
            assist_mode=2,
            profile_id=1,
            virtual_gear=3,
            flags=0xA5,
        )
        client = FakeClient(payload)
        args = uart_client.make_arg_parser().parse_args(
            ["stream", "--period-ms", "100", "--duration-ms", "100"]
        )
        buf = io.StringIO()

        with patch("uart_client.time.time", side_effect=[0.0, 0.0, 1.0]):
            with redirect_stdout(buf):
                output = uart_client.run_command(client, args)

        self.assertEqual(client.period_ms, 100)
        self.assertEqual(client.frames_read, 1)
        self.assertIsNone(output)
        line = buf.getvalue().strip()
        self.assertIn("captured 1 frames; first t=123ms", line)
        self.assertIn("spd=456", line)
        self.assertIn("cad=78", line)
        self.assertIn("pwr=250", line)
        self.assertIn("batt=48.0V 3.5A", line)
        self.assertIn("temp=25.0C", line)
        self.assertIn("profile=1", line)
        self.assertIn("gear=3", line)
        self.assertIn("flags=0xa5", line)


if __name__ == "__main__":
    unittest.main()
