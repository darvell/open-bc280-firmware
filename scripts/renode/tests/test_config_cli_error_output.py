import json
import os
import sys
import tempfile
import unittest

SCRIPT_DIR = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
if SCRIPT_DIR not in sys.path:
    sys.path.insert(0, SCRIPT_DIR)

import uart_client


class FakeClient:
    def __init__(self, cfg: uart_client.ConfigBlob) -> None:
        self._cfg = cfg

    def config_get(self) -> uart_client.ConfigBlob:
        return self._cfg

    def close(self) -> None:
        pass


class ConfigCliErrorOutputTests(unittest.TestCase):
    def test_cli_reports_invalid_json_error(self) -> None:
        with tempfile.NamedTemporaryFile(mode="w", suffix=".json", delete=False) as tmp:
            json.dump({"unknown_field": 1}, tmp)
            path = tmp.name
        try:
            args = uart_client.make_arg_parser().parse_args(["config-diff", "--from-file", path])
            cfg = uart_client.ConfigBlob.defaults()
            client = FakeClient(cfg)
            with self.assertRaises(ValueError) as ctx:
                uart_client.run_command(client, args)
            self.assertIn("unknown config fields", str(ctx.exception))
        finally:
            os.unlink(path)


if __name__ == "__main__":
    unittest.main()
