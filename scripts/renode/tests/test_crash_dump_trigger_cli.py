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

    def crash_dump_trigger(self) -> int:
        self.called = True
        return 0x08001234

    def close(self) -> None:
        pass


class CrashDumpTriggerCliTests(unittest.TestCase):
    def test_crash_dump_trigger_emits_status(self) -> None:
        args = uart_client.make_arg_parser().parse_args(["crash-dump-trigger"])
        client = FakeClient()

        output = uart_client.run_command(client, args)

        self.assertTrue(client.called)
        self.assertEqual(output, ["crash trigger sent (pc_hint=0x08001234)"])


if __name__ == "__main__":
    unittest.main()
