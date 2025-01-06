#!/usr/bin/env python3
"""Renode UI render-hash determinism + dirty budget for multiple pages."""

import os
import sys
import time
from typing import Dict, List, Optional, Tuple

from uart_client import ProtocolError, UARTClient

PORT = os.environ.get("BC280_UART1_PTY", "/tmp/uart1")
OUTDIR = os.environ.get("BC280_RENODE_OUTDIR") or os.path.abspath(
    os.path.join(os.path.dirname(__file__), "..", "..", "open-firmware", "renode", "output")
)
UART_LOG = os.path.join(OUTDIR, "uart1_tx.log")

BTN_NEXT = 0x08
BTN_NONE = 0x00

MAX_DIRTY = 12
RENDER_BUDGET_MS = 200

ALLOWED_NAMES = {
    "dashboard",
    "focus",
    "settings",
    "profiles",
    "about",
    "battery",
    "thermal",
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


def parse_kv(line: str) -> Dict[str, str]:
    parts = line.strip().split()
    kv: Dict[str, str] = {}
    for token in parts[2:]:
        if "=" not in token:
            continue
        key, val = token.split("=", 1)
        kv[key] = val
    return kv


def parse_ui_reg(line: str) -> Tuple[List[int], List[str]]:
    kv = parse_kv(line)
    layout_raw = kv.get("layout", "")
    names_raw = kv.get("names", "")
    layout = [int(x) for x in layout_raw.split(",") if x]
    names = [x for x in names_raw.split(",") if x]
    return layout, names


def parse_ui_nav(line: str) -> Optional[Tuple[int, int, int]]:
    kv = parse_kv(line)
    try:
        ms = int(kv.get("ms", "0"))
        frm = int(kv.get("from", "-1"))
        to = int(kv.get("to", "-1"))
    except ValueError:
        return None
    return ms, frm, to


def parse_ui_trace(line: str) -> Optional[Dict[str, int]]:
    kv = parse_kv(line)
    out: Dict[str, int] = {}
    for key in ("ms", "hash", "dirty", "dt"):
        val = kv.get(key)
        if val is None:
            continue
        try:
            out[key] = int(val)
        except ValueError:
            return None
    if "hash" not in out:
        return None
    return out


def load_events(path: str, offset: int) -> Tuple[Optional[Tuple[List[int], List[str]]], List[Tuple[str, object]]]:
    events: List[Tuple[str, object]] = []
    registry: Optional[Tuple[List[int], List[str]]] = None
    if not os.path.exists(path):
        return registry, events
    with open(path, "r", errors="ignore") as f:
        if offset:
            f.seek(offset)
        for raw in f:
            line = raw.strip()
            if line.startswith("[TRACE] ui-reg"):
                registry = parse_ui_reg(line)
            elif line.startswith("[TRACE] ui-nav"):
                nav = parse_ui_nav(line)
                if nav:
                    events.append(("nav", nav))
            elif line.startswith("[TRACE] ui "):
                trace = parse_ui_trace(line)
                if trace:
                    events.append(("ui", trace))
    return registry, events


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

    base = dict(
        rpm=0,
        torque=0,
        speed_dmph=0,
        soc=80,
        err=0,
        cadence_rpm=0,
        throttle_pct=0,
        brake=0,
        power_w=0,
        batt_dV=480,
        batt_dA=0,
        ctrl_temp_dC=250,
    )

    client = UARTClient(PORT, baud=115200, timeout=0.5)
    try:
        client.ping()
        client.set_state(**base, buttons=BTN_NONE)
        time.sleep(0.6)

        for _ in range(5):
            client.set_state(**base, buttons=BTN_NEXT)
            time.sleep(0.2)
            client.set_state(**base, buttons=BTN_NONE)
            time.sleep(0.6)

        time.sleep(0.2)

        registry, events = load_events(UART_LOG, 0)
        expect(registry is not None, "no ui-reg trace found (RENODE_TEST build required)")
        layout, names = registry
        expect(len(layout) == len(names) and len(layout) > 0, "ui-reg layout/names missing")

        name_map = {layout[i]: names[i] for i in range(len(layout))}
        allowed_ids = {pid for pid, name in name_map.items() if name in ALLOWED_NAMES}
        expect(len(allowed_ids) >= 3, "not enough stable pages in ui-reg layout")

        current_page: Optional[int] = None
        page_traces: Dict[int, List[Dict[str, int]]] = {}
        page_order: List[int] = []

        for etype, data in events:
            if etype == "nav":
                _, frm, to = data
                if current_page is None:
                    current_page = frm
                current_page = to
            elif etype == "ui" and current_page is not None:
                if current_page in allowed_ids:
                    if current_page not in page_traces:
                        page_traces[current_page] = []
                        page_order.append(current_page)
                    page_traces[current_page].append(data)

        selected = [pid for pid in page_order if len(page_traces.get(pid, [])) >= 2]
        expect(len(selected) >= 3, "insufficient ui traces for 3 pages")

        for pid in selected[:3]:
            traces = page_traces[pid]
            prev = traces[-2]
            last = traces[-1]
            name = name_map.get(pid, "unknown")
            expect(prev["hash"] == last["hash"], f"hash unstable for {name} (page {pid})")
            for t in (prev, last):
                expect(t.get("dirty", 0) <= MAX_DIRTY, f"dirty={t.get('dirty')} exceeds {MAX_DIRTY} on {name}")
                expect(t.get("dt", 0) <= RENDER_BUDGET_MS, f"render dt={t.get('dt')} exceeds budget on {name}")

        print("PASS: ui render hashes + dirty budget for multiple pages")
        return 0
    except (ProtocolError, AssertionError) as e:
        sys.stderr.write(f"FAIL: {e}\n")
        return 1
    finally:
        client.close()


if __name__ == "__main__":
    raise SystemExit(main())
