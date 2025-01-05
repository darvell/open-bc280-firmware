import os
import sys
import unittest
from unittest import mock

SCRIPT_DIR = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
if SCRIPT_DIR not in sys.path:
    sys.path.insert(0, SCRIPT_DIR)

import uart_client


class FakeSerial:
    def __init__(self, response: bytes) -> None:
        self._buf = bytearray(response)
        self.writes = []

    def close(self) -> None:
        pass

    def write(self, data: bytes) -> None:
        self.writes.append(bytes(data))

    def read_exact(self, n: int) -> bytes:
        if len(self._buf) < n:
            raise uart_client.ProtocolError(f"fake serial underrun ({len(self._buf)} < {n})")
        out = bytes(self._buf[:n])
        del self._buf[:n]
        return out

    def read_any(self, n: int = 1) -> bytes:
        if not self._buf:
            return b""
        n = min(n, len(self._buf))
        out = bytes(self._buf[:n])
        del self._buf[:n]
        return out


class UartClientSetBootloaderFlagTests(unittest.TestCase):
    def test_set_bootloader_flag_sends_frame(self) -> None:
        resp = bytes([uart_client.SOF, 0x8B, 1, 0x00])
        resp += bytes([uart_client.checksum(resp)])
        fake = FakeSerial(resp)

        with mock.patch.object(uart_client, "RawSerial", return_value=fake):
            client = uart_client.UARTClient("/tmp/fake")
            client.set_bootloader_flag()

        self.assertEqual(len(fake.writes), 1)
        sent = fake.writes[0]
        self.assertEqual(sent[0], uart_client.SOF)
        self.assertEqual(sent[1], 0x0B)
        self.assertEqual(sent[2], 0)
        self.assertEqual(sent[-1], uart_client.checksum(sent[:-1]))


if __name__ == "__main__":
    unittest.main()
