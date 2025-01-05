import json
import os
import sys
import tempfile
import unittest

SCRIPT_DIR = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
if SCRIPT_DIR not in sys.path:
    sys.path.insert(0, SCRIPT_DIR)

import uart_client


class ConfigJsonCurveRangeTests(unittest.TestCase):
    def test_config_from_json_rejects_negative_curve_values(self) -> None:
        with tempfile.NamedTemporaryFile(mode="w", suffix=".json", delete=False) as tmp:
            json.dump({"curve": [[-1, 2]]}, tmp)
            path = tmp.name
        try:
            with self.assertRaises(ValueError):
                uart_client.config_from_json(path)
        finally:
            os.unlink(path)

    def test_config_from_json_rejects_curve_value_overflow(self) -> None:
        with tempfile.NamedTemporaryFile(mode="w", suffix=".json", delete=False) as tmp:
            json.dump({"curve": [[70000, 1]]}, tmp)
            path = tmp.name
        try:
            with self.assertRaises(ValueError):
                uart_client.config_from_json(path)
        finally:
            os.unlink(path)


if __name__ == "__main__":
    unittest.main()
