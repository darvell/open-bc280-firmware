#!/usr/bin/env python3
"""Bootloader OTA sender for BC280 (UART frame format)."""
from __future__ import annotations

import time
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable


SOF = 0x55
CMD_INIT = 0x22
CMD_WRITE = 0x24
CMD_DONE = 0x26

RESP_INIT = 0x23
RESP_WRITE = 0x25
RESP_DONE = 0x27

BLOCK_SIZE = 0x80


def _checksum_invert_xor(data: Iterable[int]) -> int:
    acc = 0
    for b in data:
        acc ^= int(b) & 0xFF
    return (~acc) & 0xFF


def build_frame(cmd: int, payload: bytes) -> bytes:
    cmd = int(cmd) & 0xFF
    if payload is None:
        payload = b""
    if len(payload) > 0x92:
        raise ValueError("payload too large for bootloader frame")
    frame = bytearray()
    frame.append(SOF)
    frame.append(cmd)
    frame.append(len(payload) & 0xFF)
    frame.extend(payload)
    frame.append(_checksum_invert_xor(frame))
    return bytes(frame)


def calc_crc8_maxim(data: bytes, size_bytes: int) -> int:
    """CRC-8/Maxim (poly 0x31 reflected = 0x8C), init 0x00."""
    size_bytes = int(size_bytes)
    end = size_bytes & ~3
    crc = 0
    for b in data[:end]:
        crc ^= b
        for _ in range(8):
            if crc & 1:
                crc = (crc >> 1) ^ 0x8C
            else:
                crc >>= 1
            crc &= 0xFF
    return crc & 0xFF


@dataclass
class Frame:
    cmd: int
    payload: bytes


class FrameReader:
    def __init__(self, path: Path, offset: int = 0):
        self.path = Path(path)
        self.offset = int(offset)
        self._buf = bytearray()

    def _read_new(self) -> None:
        if not self.path.exists():
            return
        data = self.path.read_bytes()
        if self.offset >= len(data):
            return
        self._buf.extend(data[self.offset :])
        self.offset = len(data)

    def read_frames(self) -> list[Frame]:
        self._read_new()
        frames: list[Frame] = []
        i = 0
        buf = self._buf
        while i < len(buf):
            if buf[i] != SOF:
                i += 1
                continue
            if i + 3 >= len(buf):
                break
            payload_len = buf[i + 2]
            total = payload_len + 4
            if i + total > len(buf):
                break
            frame = buf[i : i + total]
            chk = _checksum_invert_xor(frame[:-1])
            if chk != frame[-1]:
                i += 1
                continue
            frames.append(Frame(cmd=frame[1], payload=bytes(frame[3:-1])))
            i += total
        if i:
            del buf[:i]
        return frames


class BootloaderOTASender:
    def __init__(
        self,
        rx_path: Path | None,
        tx_log: Path,
        log_path: Path | None = None,
        chunk: int = 16,
        delay_s: float = 0.002,
        inter_frame_delay_s: float = 0.01,
        write_fn=None,
    ):
        self.rx_path = Path(rx_path) if rx_path else None
        self.tx_log = Path(tx_log)
        self.log_path = Path(log_path) if log_path else None
        self.chunk = int(chunk)
        self.delay_s = float(delay_s)
        self.inter_frame_delay_s = float(inter_frame_delay_s)
        self.write_fn = write_fn
        offset = self.tx_log.stat().st_size if self.tx_log.exists() else 0
        self.reader = FrameReader(self.tx_log, offset=offset)

    def _log(self, msg: str) -> None:
        if not self.log_path:
            return
        try:
            self.log_path.parent.mkdir(parents=True, exist_ok=True)
            with self.log_path.open("a") as f:
                f.write(msg.rstrip() + "\n")
        except Exception:
            pass

    def _send(self, frame: bytes) -> None:
        # Throttle writes so the bootloader parser can keep up with the RX ring.
        if self.write_fn is None:
            if not self.rx_path:
                raise RuntimeError("rx_path is required when no write_fn is provided")
            with self.rx_path.open("ab") as f:
                if self.chunk <= 0:
                    f.write(frame)
                    f.flush()
                    if self.inter_frame_delay_s > 0:
                        time.sleep(self.inter_frame_delay_s)
                    return
                for off in range(0, len(frame), self.chunk):
                    f.write(frame[off : off + self.chunk])
                    f.flush()
                    if self.delay_s > 0:
                        time.sleep(self.delay_s)
                if self.inter_frame_delay_s > 0:
                    time.sleep(self.inter_frame_delay_s)
            return
        if self.chunk <= 0:
            self.write_fn(frame)
            if self.inter_frame_delay_s > 0:
                time.sleep(self.inter_frame_delay_s)
            return
        for off in range(0, len(frame), self.chunk):
            self.write_fn(frame[off : off + self.chunk])
            if self.delay_s > 0:
                time.sleep(self.delay_s)
        if self.inter_frame_delay_s > 0:
            time.sleep(self.inter_frame_delay_s)

    def _wait_for_status(self, cmd: int, value: int, timeout_s: float) -> bool:
        deadline = time.time() + timeout_s
        while time.time() < deadline:
            frames = self.reader.read_frames()
            for fr in frames:
                if fr.cmd != cmd:
                    continue
                if len(fr.payload) != 1:
                    continue
                if fr.payload[0] == (value & 0xFF):
                    return True
                self._log(f"status cmd=0x{cmd:02X} value=0x{fr.payload[0]:02X} (wanted 0x{value & 0xFF:02X})")
            time.sleep(0.02)
        return False

    def push_image(self, image_path: Path, timeout_init_s: float = 6.0, timeout_block_s: float = 6.0) -> bool:
        image_path = Path(image_path)
        data = image_path.read_bytes()
        size = len(data)
        crc8 = calc_crc8_maxim(data, size)

        init_payload = bytes([crc8]) + size.to_bytes(4, "big")
        self._log(f"init size={size} crc8=0x{crc8:02X}")
        self._send(build_frame(CMD_INIT, init_payload))
        if not self._wait_for_status(RESP_INIT, 1, timeout_init_s):
            self._log("init failed or timed out")
            return False

        blocks = (size + BLOCK_SIZE - 1) // BLOCK_SIZE
        for idx in range(blocks):
            start = idx * BLOCK_SIZE
            chunk = data[start : start + BLOCK_SIZE]
            if len(chunk) < BLOCK_SIZE:
                chunk = chunk + bytes([0xFF]) * (BLOCK_SIZE - len(chunk))
            # Bootloader expects a 4-byte big-endian block index.
            payload = idx.to_bytes(4, "big") + chunk
            self._send(build_frame(CMD_WRITE, payload))
            if not self._wait_for_status(RESP_WRITE, 0, timeout_block_s):
                self._log(f"block {idx} failed or timed out")
                return False
            if idx % 64 == 0:
                self._log(f"block {idx+1}/{blocks} ok")

        self._send(build_frame(CMD_DONE, b""))
        if not self._wait_for_status(RESP_DONE, 1, timeout_init_s):
            self._log("finalize failed or timed out")
            return False
        return True
