import io
import os
import sys
import unittest
from unittest import mock
from contextlib import redirect_stdout

SCRIPT_DIR = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
if SCRIPT_DIR not in sys.path:
    sys.path.insert(0, SCRIPT_DIR)

import uart_client


class RenodeSmokeMainCliOutputTests(unittest.TestCase):
    def test_main_prints_output_from_renode_smoke(self) -> None:
        class FakeClient:
            def __init__(self, port: str, baud: int) -> None:
                self.port = port
                self.baud = baud

            def renode_smoke(self) -> None:
                pass

            def close(self) -> None:
                pass

        buf = io.StringIO()
        with redirect_stdout(buf):
            with mock.patch.object(uart_client, "UARTClient", FakeClient):
                rc = uart_client.main(["--renode-smoke"])

        self.assertEqual(rc, 0)
        self.assertEqual(buf.getvalue().strip(), "renode smoke complete")


if __name__ == "__main__":
    unittest.main()
