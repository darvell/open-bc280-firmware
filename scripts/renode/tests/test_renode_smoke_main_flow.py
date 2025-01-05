import os
import sys
import unittest
from unittest import mock

SCRIPT_DIR = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
if SCRIPT_DIR not in sys.path:
    sys.path.insert(0, SCRIPT_DIR)

import uart_client


class RenodeSmokeMainFlowTests(unittest.TestCase):
    def test_main_runs_renode_smoke_flow(self) -> None:
        with mock.patch.object(uart_client, "run_command", return_value=None) as run_cmd:
            with mock.patch.object(uart_client, "UARTClient") as uart_ctor:
                rc = uart_client.main(["--renode-smoke", "--port", "/tmp/uart1"])

        self.assertEqual(rc, 0)
        uart_ctor.assert_called_once_with("/tmp/uart1", baud=115200)
        client = uart_ctor.return_value
        client.close.assert_called_once()
        args = run_cmd.call_args.args[1]
        self.assertTrue(args.renode_smoke)
        self.assertEqual(args.subcmd, "renode-smoke")


if __name__ == "__main__":
    unittest.main()
