import json
import os
import sys
import tempfile
import unittest

SCRIPT_DIR = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
if SCRIPT_DIR not in sys.path:
    sys.path.insert(0, SCRIPT_DIR)

from uart_client import config_from_json, ConfigBlob


class ConfigJsonLoadTests(unittest.TestCase):
    def test_config_from_json_overrides_fields(self) -> None:
        payload = {
            "wheel_mm": 2050,
            "units": 1,
            "curve": [[1, 2], [3, 4]],
        }
        with tempfile.NamedTemporaryFile(mode="w", suffix=".json", delete=False) as tmp:
            json.dump(payload, tmp)
            path = tmp.name
        try:
            cfg = config_from_json(path)
        finally:
            os.unlink(path)

        self.assertIsInstance(cfg, ConfigBlob)
        self.assertEqual(cfg.wheel_mm, 2050)
        self.assertEqual(cfg.units, 1)
        self.assertEqual(cfg.curve[0], (1, 2))
        self.assertEqual(cfg.curve[1], (3, 4))


if __name__ == "__main__":
    unittest.main()
