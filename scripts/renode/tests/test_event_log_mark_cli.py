import os
import sys
import unittest

SCRIPT_DIR = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
if SCRIPT_DIR not in sys.path:
    sys.path.insert(0, SCRIPT_DIR)

import uart_client


class FakeClient:
    def __init__(self) -> None:
        self.calls = []

    def log_event_mark(self, rtype: int, flags: int = 0) -> None:
        self.calls.append((rtype, flags))

    def close(self) -> None:
        pass


class EventLogMarkCliTests(unittest.TestCase):
    def test_event_log_mark_emits_confirmation(self) -> None:
        args = uart_client.make_arg_parser().parse_args(
            ["event-log-mark", "--type", "3", "--flags", "2"]
        )
        client = FakeClient()

        output = uart_client.run_command(client, args)

        self.assertEqual(client.calls, [(3, 2)])
        self.assertEqual(output, ["event marked"])


if __name__ == "__main__":
    unittest.main()
