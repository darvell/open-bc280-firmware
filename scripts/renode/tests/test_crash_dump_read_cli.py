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

    def crash_dump_read(self) -> uart_client.CrashDump:
        self.called = True
        return uart_client.CrashDump(
            magic=uart_client.CRASH_DUMP_MAGIC,
            version=1,
            size=72,
            flags=0,
            seq=4,
            crc32=0,
            ms=1234,
            sp=0x20000000,
            lr=0x08001000,
            pc=0x08002000,
            psr=0x21000000,
            cfsr=0,
            hfsr=0,
            dfsr=0,
            mmfar=0,
            bfar=0,
            afsr=0,
            event_count=1,
            event_record_size=20,
            event_seq=7,
            event_records=[
                uart_client.EventRecord(
                    ms=1234,
                    type=2,
                    flags=1,
                    speed_dmph=12,
                    batt_dV=34,
                    batt_dA=56,
                    temp_dC=78,
                    cmd_power_w=90,
                    cmd_current_dA=12,
                )
            ],
            crc_ok=True,
        )

    def close(self) -> None:
        pass


class CrashDumpReadCliTests(unittest.TestCase):
    def test_crash_dump_read_returns_dict(self) -> None:
        args = uart_client.make_arg_parser().parse_args(["crash-dump-read"])
        client = FakeClient()

        output = uart_client.run_command(client, args)

        self.assertTrue(client.called)
        self.assertIsInstance(output, list)
        self.assertEqual(output[0]["magic"], uart_client.CRASH_DUMP_MAGIC)
        self.assertEqual(output[0]["event_count"], 1)
        self.assertEqual(output[0]["event_records"][0]["type"], 2)


if __name__ == "__main__":
    unittest.main()
