import os
import sys
import unittest

SCRIPT_DIR = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
if SCRIPT_DIR not in sys.path:
    sys.path.insert(0, SCRIPT_DIR)

from uart_client import ConfigBlob, format_config_diff_output


class ConfigCliDiffTests(unittest.TestCase):
    def test_format_config_diff_lists_changed_fields(self) -> None:
        base = ConfigBlob.defaults()
        other = ConfigBlob.defaults()
        other.wheel_mm = base.wheel_mm + 25
        other.units = 1
        other.curve = list(base.curve)
        other.curve[1] = (111, 222)

        lines = format_config_diff_output(base, other)
        joined = "\n".join(lines)

        self.assertIn("wheel_mm:", joined)
        self.assertIn("units:", joined)
        self.assertIn("curve:", joined)
        self.assertTrue(lines)

    def test_format_config_diff_empty_when_no_changes(self) -> None:
        base = ConfigBlob.defaults()
        other = ConfigBlob.defaults()

        lines = format_config_diff_output(base, other)

        self.assertEqual(lines, ["no changes"])


if __name__ == "__main__":
    unittest.main()
