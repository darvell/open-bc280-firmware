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

    def config_get(self) -> uart_client.ConfigBlob:
        self.called = True
        cfg = uart_client.ConfigBlob.defaults()
        cfg.units = 1
        return cfg

    def close(self) -> None:
        pass


class ConfigCliGetTests(unittest.TestCase):
    def test_config_get_returns_dict(self) -> None:
        args = uart_client.make_arg_parser().parse_args(["config-get"])
        client = FakeClient()

        output = uart_client.run_command(client, args)

        self.assertTrue(client.called)
        self.assertIsInstance(output, list)
        self.assertEqual(len(output), 1)
        self.assertIsInstance(output[0], dict)
        self.assertEqual(output[0]["units"], 1)


if __name__ == "__main__":
    unittest.main()
