import json
import os
import sys
import tempfile
import unittest
from typing import Any

SCRIPT_DIR = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
if SCRIPT_DIR not in sys.path:
    sys.path.insert(0, SCRIPT_DIR)

import uart_client


class FakeClient:
    def __init__(self, cfg: uart_client.ConfigBlob) -> None:
        self._cfg = cfg
        self.closed = False

    def config_get(self) -> uart_client.ConfigBlob:
        return self._cfg

    def close(self) -> None:
        self.closed = True


def _fake_make_parser() -> tuple[Any, str]:
    parser = uart_client.make_arg_parser()
    tmp = tempfile.NamedTemporaryFile(mode="w", suffix=".json", delete=False)
    json.dump({"units": 1}, tmp)
    tmp.flush()
    tmp.close()
    return parser, tmp.name


def _fake_make_bad_parser() -> tuple[Any, str]:
    parser = uart_client.make_arg_parser()
    tmp = tempfile.NamedTemporaryFile(mode="w", suffix=".json", delete=False)
    json.dump({"unknown_field": 1}, tmp)
    tmp.flush()
    tmp.close()
    return parser, tmp.name


class ConfigCliFromFileTests(unittest.TestCase):
    def test_config_diff_from_file_passes_baseline(self) -> None:
        parser, path = _fake_make_parser()
        try:
            args = parser.parse_args(["config-diff", "--from-file", path])
            cfg = uart_client.ConfigBlob.defaults()
            cfg.units = 1
            client = FakeClient(cfg)
            called = {}

            def fake_format(base, other):
                called["base"] = base
                called["other"] = other
                return ["ok"]

            orig = uart_client.format_config_diff_output
            uart_client.format_config_diff_output = fake_format
            try:
                output = uart_client.run_command(client, args)
            finally:
                uart_client.format_config_diff_output = orig

            self.assertEqual(output, ["ok"])
            self.assertEqual(called["base"].units, 1)
            self.assertEqual(called["other"].units, 1)
        finally:
            os.unlink(path)

    def test_config_diff_from_file_rejects_invalid_json(self) -> None:
        parser, path = _fake_make_bad_parser()
        try:
            args = parser.parse_args(["config-diff", "--from-file", path])
            cfg = uart_client.ConfigBlob.defaults()
            client = FakeClient(cfg)
            with self.assertRaises(ValueError):
                uart_client.run_command(client, args)
        finally:
            os.unlink(path)


if __name__ == "__main__":
    unittest.main()
