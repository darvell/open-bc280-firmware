import os
import sys
import unittest

SCRIPT_DIR = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
if SCRIPT_DIR not in sys.path:
    sys.path.insert(0, SCRIPT_DIR)

import uart_client


class FakeClient:
    def ping(self) -> None:
        raise AssertionError("ping should not be called")

    def renode_smoke(self) -> None:
        raise AssertionError("renode_smoke should not be called")

    def close(self) -> None:
        pass


class RenodeSmokeConflictCliTests(unittest.TestCase):
    def test_renode_smoke_with_subcommand_errors(self) -> None:
        args = uart_client.make_arg_parser().parse_args(["--renode-smoke", "ping"])
        client = FakeClient()

        with self.assertRaises(uart_client.ProtocolError) as ctx:
            uart_client.run_command(client, args)

        self.assertIn("cannot combine --renode-smoke", str(ctx.exception))


if __name__ == "__main__":
    unittest.main()
