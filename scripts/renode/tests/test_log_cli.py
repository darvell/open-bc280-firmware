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

    def log_frame(self) -> bytes:
        self.called = True
        return bytes([0x12, 0x03, 0x01, 0x02, 0x03])

    def close(self) -> None:
        pass


class LogCliTests(unittest.TestCase):
    def test_log_prints_payload(self) -> None:
        args = uart_client.make_arg_parser().parse_args(["log"])
        client = FakeClient()

        output = uart_client.run_command(client, args)

        self.assertTrue(client.called)
        self.assertEqual(output, ["code=0x12 len=3 data=010203"])

    def test_log_prints_empty_payload(self) -> None:
        class EmptyClient(FakeClient):
            def log_frame(self) -> bytes:
                self.called = True
                return bytes([0x12])

        args = uart_client.make_arg_parser().parse_args(["log"])
        client = EmptyClient()

        output = uart_client.run_command(client, args)

        self.assertTrue(client.called)
        self.assertEqual(output, [""])

    def test_log_prints_length_mismatch(self) -> None:
        class MismatchClient(FakeClient):
            def log_frame(self) -> bytes:
                self.called = True
                return bytes([0x12, 0x05, 0x01, 0x02])

        args = uart_client.make_arg_parser().parse_args(["log"])
        client = MismatchClient()

        output = uart_client.run_command(client, args)

        self.assertTrue(client.called)
        self.assertEqual(
            output,
            ["code=0x12 len=5 data=0102 (len_mismatch)"],
        )


if __name__ == "__main__":
    unittest.main()
