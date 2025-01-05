import os
import sys
import unittest

SCRIPT_DIR = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
if SCRIPT_DIR not in sys.path:
    sys.path.insert(0, SCRIPT_DIR)

from uart_client import ConfigBlob, CONFIG_DIFF_IGNORE


class ConfigBlobDiffTests(unittest.TestCase):
    def test_diff_reports_changed_fields(self) -> None:
        base = ConfigBlob.defaults()
        updated = ConfigBlob.defaults()
        updated.wheel_mm = base.wheel_mm - 100
        updated.units = 1
        updated.curve = list(base.curve)
        updated.curve[0] = (123, 456)
        updated.seq = base.seq + 1
        updated.crc32 = 0x12345678

        diffs = base.diff(updated)
        diff_fields = [field for field, _old, _new in diffs]

        self.assertIn("wheel_mm", diff_fields)
        self.assertIn("units", diff_fields)
        self.assertIn("curve", diff_fields)
        self.assertNotIn("seq", diff_fields)
        self.assertNotIn("crc32", diff_fields)
        self.assertTrue(CONFIG_DIFF_IGNORE.issuperset({"seq", "crc32"}))


if __name__ == "__main__":
    unittest.main()
