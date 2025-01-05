#!/usr/bin/env python3
"""Renode regression for UI themes + palette persistence."""

import os
import sys
import time
from typing import Dict, List, Optional

from uart_client import ProtocolError, UARTClient

PORT = os.environ.get("BC280_UART1_PTY", "/tmp/uart1")
OUTDIR = os.environ.get("BC280_RENODE_OUTDIR") or os.path.abspath(
    os.path.join(os.path.dirname(__file__), "..", "..", "out", "renode")
)
UART_LOG = os.path.join(OUTDIR, "uart1_tx.log")

EXPECTED_PALETTES: Dict[int, List[int]] = {
    0: [0xFFFF, 0xE73C, 0x0000, 0x7BEF, 0x219F, 0xFFE0, 0xF800, 0x07E0],
    1: [0x0000, 0x39C7, 0xFFFF, 0x7BEF, 0x07FF, 0xFD20, 0xF800, 0x07E0],
    2: [0x0000, 0xFFFF, 0x0000, 0xFFFF, 0xFFE0, 0xFFE0, 0xF800, 0x07E0],
    3: [0xFFFF, 0xDEFB, 0x0000, 0x7BEF, 0x001F, 0xFD20, 0xF81F, 0x07FF],
}


def expect(cond: bool, msg: str) -> None:
    if not cond:
        raise AssertionError(msg)


def wait_for_pty(path: str, timeout_s: float = 10.0) -> None:
    deadline = time.time() + timeout_s
    while time.time() < deadline:
        if os.path.exists(path):
            return
        time.sleep(0.1)
    raise FileNotFoundError(f"UART PTY not found at {path}")


def wait_for_ping(client: UARTClient, tries: int = 15, delay: float = 0.1) -> None:
    for _ in range(tries):
        try:
            client.ping()
            return
        except Exception:
            time.sleep(delay)
    raise ProtocolError("ping did not recover after reboot")


def parse_trace_log(path: str) -> List[Dict[str, object]]:
    entries: List[Dict[str, object]] = []
    if not os.path.exists(path):
        return entries
    with open(path, "r", errors="ignore") as f:
        for raw in f:
            line = raw.strip()
            if not line.startswith("[TRACE] ui"):
                continue
            parts = line.split()
            kv: Dict[str, object] = {}
            for token in parts[2:]:
                if "=" not in token:
                    continue
                key, val = token.split("=", 1)
                if key == "pal":
                    try:
                        kv["pal"] = [int(x, 16) for x in val.split(",") if x]
                    except ValueError:
                        continue
                else:
                    try:
                        kv[key] = int(val)
                    except ValueError:
                        continue
            if "theme" in kv:
                entries.append(kv)
    return entries


def wait_for_theme(path: str, theme_id: int, timeout_s: float = 3.0) -> Optional[Dict[str, object]]:
    deadline = time.time() + timeout_s
    while time.time() < deadline:
        entries = parse_trace_log(path)
        if entries:
            for entry in reversed(entries):
                if entry.get("theme") == theme_id:
                    return entry
        time.sleep(0.05)
    return None


def apply_theme(client: UARTClient, theme_id: int) -> None:
    cfg = client.config_get()
    cfg.theme = theme_id
    cfg.seq = cfg.seq + 1
    client.config_stage(cfg)
    client.config_commit(reboot=True)


def main() -> int:
    try:
        wait_for_pty(PORT)
    except FileNotFoundError as e:
        sys.stderr.write(str(e) + "\n")
        return 1

    try:
        if os.path.exists(UART_LOG):
            os.remove(UART_LOG)
    except Exception:
        pass

    client = UARTClient(PORT, baud=115200, timeout=0.5)
    try:
        wait_for_ping(client)

        for theme_id in (1, 3):
            apply_theme(client, theme_id)
            time.sleep(0.3)
            wait_for_ping(client)

            updated = client.config_get()
            expect(updated.theme == theme_id, f"theme {theme_id} not persisted (got {updated.theme})")

            entry = wait_for_theme(UART_LOG, theme_id, timeout_s=3.0)
            expect(entry is not None, f"no ui trace found for theme {theme_id}")
            pal = entry.get("pal")
            expect(isinstance(pal, list) and len(pal) == 8, "palette list missing in ui trace")
            expected = EXPECTED_PALETTES[theme_id]
            expect(pal == expected, f"palette mismatch for theme {theme_id}: {pal} != {expected}")

        print("PASS: UI theme persists + palette trace")
        return 0
    except (ProtocolError, AssertionError) as e:
        sys.stderr.write(f"FAIL: {e}\n")
        return 1
    finally:
        client.close()


if __name__ == "__main__":
    raise SystemExit(main())
