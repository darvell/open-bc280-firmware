#!/usr/bin/env python3
"""Renode-driven UI registry + navigation test for BC280 open firmware.

Validates:
- UI registry trace reports at least 2 screens/modules.
- Layout order is exposed and names map to IDs.
- Navigation trace lines reflect next/prev movement.
"""

import os
import sys
import time
from typing import Dict, List, Tuple

from uart_client import ProtocolError, UARTClient

PORT = os.environ.get("BC280_UART1_PTY", "/tmp/uart1")
OUTDIR = os.environ.get("BC280_RENODE_OUTDIR") or os.path.abspath(
    os.path.join(os.path.dirname(__file__), "..", "..", "open-firmware", "renode", "output")
)
UART_LOG = os.path.join(OUTDIR, "uart1_tx.log")

BTN_PREV = 0x04
BTN_NEXT = 0x08


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


def parse_trace_kv(line: str) -> Dict[str, str]:
    parts = line.strip().split()
    kv: Dict[str, str] = {}
    for token in parts[2:]:
        if "=" not in token:
            continue
        key, val = token.split("=", 1)
        kv[key] = val
    return kv


def load_traces(path: str, offset: int) -> Tuple[List[Dict[str, str]], List[Dict[str, str]]]:
    regs: List[Dict[str, str]] = []
    navs: List[Dict[str, str]] = []
    if not os.path.exists(path):
        return regs, navs
    with open(path, "r", errors="ignore") as f:
        if offset > 0:
            f.seek(offset)
        for raw in f:
            line = raw.strip()
            if line.startswith("[TRACE] ui-reg"):
                regs.append(parse_trace_kv(line))
            elif line.startswith("[TRACE] ui-nav"):
                navs.append(parse_trace_kv(line))
    return regs, navs


def wait_for_traces(path: str, offset: int, min_regs: int, min_navs: int, timeout_s: float = 3.0) -> Tuple[List[Dict[str, str]], List[Dict[str, str]]]:
    deadline = time.time() + timeout_s
    regs: List[Dict[str, str]] = []
    navs: List[Dict[str, str]] = []
    while time.time() < deadline:
        regs, navs = load_traces(path, offset)
        if len(regs) >= min_regs and len(navs) >= min_navs:
            return regs, navs
        time.sleep(0.05)
    return regs, navs


def parse_layout(reg: Dict[str, str]) -> Tuple[List[int], List[str]]:
    layout_raw = reg.get("layout", "")
    names_raw = reg.get("names", "")
    layout = [int(x) for x in layout_raw.split(",") if x != ""]
    names = [x for x in names_raw.split(",") if x != ""]
    return layout, names


def main() -> int:
    try:
        wait_for_pty(PORT)
    except FileNotFoundError as e:
        sys.stderr.write(str(e) + "\n")
        return 1

    start_offset = os.path.getsize(UART_LOG) if os.path.exists(UART_LOG) else 0

    client = UARTClient(PORT, baud=115200, timeout=0.5)
    try:
        client.ping()
        client.set_state(rpm=220, torque=120, speed_dmph=120, soc=87, err=0, buttons=0)
        time.sleep(0.4)

        client.set_state(rpm=220, torque=120, speed_dmph=120, soc=87, err=0, buttons=BTN_NEXT)
        time.sleep(0.2)
        client.set_state(rpm=220, torque=120, speed_dmph=120, soc=87, err=0, buttons=BTN_PREV)
        time.sleep(0.2)

        regs, navs = wait_for_traces(UART_LOG, start_offset, min_regs=1, min_navs=2, timeout_s=2.0)
        expect(regs, "no ui-reg trace found (RENODE_TEST build required)")
        reg = regs[-1]
        count = int(reg.get("count", "0"))
        expect(count >= 2, f"ui-reg count {count} < 2")

        layout, names = parse_layout(reg)
        expect(len(layout) >= 2, "ui-reg layout missing or too short")
        expect(len(names) == len(layout), "ui-reg names/layout length mismatch")
        name_map = {layout[i]: names[i] for i in range(len(layout))}

        base_page = 0 if 0 in layout else layout[0]
        idx = layout.index(base_page) if base_page in layout else 0
        next_page = layout[(idx + 1) % len(layout)]

        def nav_match(frm: int, to: int) -> bool:
            for nav in navs:
                try:
                    n_from = int(nav.get("from", "-1"))
                    n_to = int(nav.get("to", "-1"))
                except ValueError:
                    continue
                if n_from == frm and n_to == to:
                    expected_name = name_map.get(n_to)
                    if expected_name and nav.get("name") != expected_name:
                        continue
                    return True
            return False

        expect(nav_match(base_page, next_page), f"missing nav from {base_page} to {next_page}")
        expect(nav_match(next_page, base_page), f"missing nav from {next_page} to {base_page}")

        print("PASS: ui registry + navigation")
        return 0
    except (ProtocolError, AssertionError) as e:
        sys.stderr.write(f"FAIL: {e}\n")
        return 1
    finally:
        client.close()


if __name__ == "__main__":
    raise SystemExit(main())
