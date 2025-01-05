import os
import sys
import unittest
from unittest import mock

SCRIPT_DIR = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
if SCRIPT_DIR not in sys.path:
    sys.path.insert(0, SCRIPT_DIR)

import uart_client


class RenodeSmokeMainConflictTests(unittest.TestCase):
    def test_main_rejects_renode_smoke_with_subcommand(self) -> None:
        with mock.patch.object(uart_client, "UARTClient") as uart_ctor:
            with self.assertRaises(SystemExit) as ctx:
                uart_client.main(["--renode-smoke", "ping"])

        self.assertEqual(ctx.exception.code, 2)
        uart_ctor.assert_not_called()


if __name__ == "__main__":
    unittest.main()
