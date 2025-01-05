import os
import sys
import unittest

SCRIPT_DIR = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
if SCRIPT_DIR not in sys.path:
    sys.path.insert(0, SCRIPT_DIR)

import uart_client


class FakeClient:
    def __init__(self, seq: int = 3) -> None:
        self.seq = seq
        self.staged = None
        self.committed = None

    def config_get(self) -> uart_client.ConfigBlob:
        cfg = uart_client.ConfigBlob.defaults()
        cfg.seq = self.seq
        return cfg

    def config_stage(self, cfg: uart_client.ConfigBlob) -> None:
        self.staged = cfg

    def config_commit(self, reboot: bool = False) -> None:
        self.committed = reboot

    def close(self) -> None:
        pass


class ConfigCliApplyTests(unittest.TestCase):
    def test_config_apply_stages_and_commits(self) -> None:
        args = uart_client.make_arg_parser().parse_args(
            [
                "config-apply",
                "--wheel-mm", "2100",
                "--units", "1",
                "--profile-id", "2",
                "--theme", "3",
                "--flags", "5",
                "--reboot",
            ]
        )
        client = FakeClient(seq=7)

        output = uart_client.run_command(client, args)

        self.assertEqual(output, ["config applied"])
        self.assertIsNotNone(client.staged)
        self.assertEqual(client.staged.wheel_mm, 2100)
        self.assertEqual(client.staged.units, 1)
        self.assertEqual(client.staged.profile_id, 2)
        self.assertEqual(client.staged.theme, 3)
        self.assertEqual(client.staged.flags, 5)
        self.assertEqual(client.staged.seq, 8)
        self.assertTrue(client.committed)

    def test_config_apply_no_reboot(self) -> None:
        args = uart_client.make_arg_parser().parse_args(
            [
                "config-apply",
                "--wheel-mm", "2100",
                "--units", "0",
                "--profile-id", "1",
                "--theme", "0",
                "--flags", "0",
            ]
        )
        client = FakeClient(seq=2)

        output = uart_client.run_command(client, args)

        self.assertEqual(output, ["config applied"])
        self.assertIsNotNone(client.staged)
        self.assertEqual(client.staged.seq, 3)
        self.assertFalse(client.committed)


if __name__ == "__main__":
    unittest.main()
