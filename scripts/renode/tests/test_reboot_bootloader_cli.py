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

    def reboot_bootloader(self) -> None:
        self.called = True

    def close(self) -> None:
        pass


class RebootBootloaderCliTests(unittest.TestCase):
    def test_reboot_bootloader_calls_client(self) -> None:
        args = uart_client.make_arg_parser().parse_args(["reboot-bootloader"])
        client = FakeClient()

        output = uart_client.run_command(client, args)

        self.assertTrue(client.called)
        self.assertEqual(output, ["reboot command sent"])


if __name__ == "__main__":
    unittest.main()
