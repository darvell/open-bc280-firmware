import os
import sys
import unittest

SCRIPT_DIR = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
if SCRIPT_DIR not in sys.path:
    sys.path.insert(0, SCRIPT_DIR)

import uart_client


class FakeClient:
    def __init__(self) -> None:
        self.cleared = False

    def crash_dump_clear(self) -> None:
        self.cleared = True

    def close(self) -> None:
        pass


class CrashDumpClearCliTests(unittest.TestCase):
    def test_crash_dump_clear_emits_confirmation(self) -> None:
        args = uart_client.make_arg_parser().parse_args(["crash-dump-clear"])
        client = FakeClient()

        output = uart_client.run_command(client, args)

        self.assertTrue(client.cleared)
        self.assertEqual(output, ["crash dump cleared"])


if __name__ == "__main__":
    unittest.main()
