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

    def set_bootloader_flag(self) -> None:
        self.called = True

    def close(self) -> None:
        pass


class SetBootloaderFlagCliTests(unittest.TestCase):
    def test_set_bootloader_flag_calls_client(self) -> None:
        args = uart_client.make_arg_parser().parse_args(["set-bootloader-flag"])
        client = FakeClient()

        output = uart_client.run_command(client, args)

        self.assertTrue(client.called)
        self.assertEqual(output, ["bootloader flag set"])


if __name__ == "__main__":
    unittest.main()
