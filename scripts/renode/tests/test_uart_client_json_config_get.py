import json
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
        cfg.seq = 3
        cfg.profile_id = 2
        return cfg

    def close(self) -> None:
        pass


class UartClientJsonConfigGetTests(unittest.TestCase):
    def test_config_get_json_output(self) -> None:
        parser = uart_client.make_arg_parser()
        parser.add_argument("--json", action="store_true")
        args = parser.parse_args(["--json", "config-get"])
        client = FakeClient()

        output = uart_client.run_command(client, args)
        lines = uart_client.format_cli_output(output, json_mode=True)

        self.assertTrue(client.called)
        self.assertEqual(len(lines), 1)
        payload = json.loads(lines[0])
        self.assertEqual(payload[0]["seq"], 3)
        self.assertEqual(payload[0]["profile_id"], 2)


if __name__ == "__main__":
    unittest.main()
