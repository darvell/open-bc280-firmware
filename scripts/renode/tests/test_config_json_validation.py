import json
import os
import sys
import tempfile
import unittest

SCRIPT_DIR = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
if SCRIPT_DIR not in sys.path:
    sys.path.insert(0, SCRIPT_DIR)

import uart_client


class ConfigJsonValidationTests(unittest.TestCase):
    def test_config_from_json_rejects_unknown_field(self) -> None:
        with tempfile.NamedTemporaryFile(mode="w", suffix=".json", delete=False) as tmp:
            json.dump({"not_a_field": 1}, tmp)
            path = tmp.name
        try:
            with self.assertRaises(ValueError):
                uart_client.config_from_json(path)
        finally:
            os.unlink(path)

    def test_config_from_json_rejects_bad_curve_shape(self) -> None:
        with tempfile.NamedTemporaryFile(mode="w", suffix=".json", delete=False) as tmp:
            json.dump({"curve": [1, 2, 3]}, tmp)
            path = tmp.name
        try:
            with self.assertRaises(ValueError):
                uart_client.config_from_json(path)
        finally:
            os.unlink(path)


if __name__ == "__main__":
    unittest.main()
