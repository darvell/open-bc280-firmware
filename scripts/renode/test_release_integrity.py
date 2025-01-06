#!/usr/bin/env python3
"""Renode UART log checks for release signing labels/rejection."""

import os
import sys
import time

EXPECT = os.environ.get("BC280_EXPECT_SIG", "unsigned")
OUTDIR = os.environ.get("BC280_RENODE_OUTDIR") or os.path.abspath(
    os.path.join(os.path.dirname(__file__), "..", "..", "open-firmware", "renode", "output")
)
UART_LOG = os.path.join(OUTDIR, "uart1_tx.log")
TIMEOUT_S = float(os.environ.get("BC280_SIG_TIMEOUT", "6"))


def read_lines() -> list:
    try:
        with open(UART_LOG, "r", errors="ignore") as f:
            return [ln.strip() for ln in f.readlines()]
    except FileNotFoundError:
        return []


def wait_for_log():
    deadline = time.time() + TIMEOUT_S
    while time.time() < deadline:
        if os.path.exists(UART_LOG):
            return
        time.sleep(0.1)


def expect(cond: bool, msg: str):
    if not cond:
        raise AssertionError(msg)


def main() -> int:
    wait_for_log()
    lines = read_lines()
    if not lines:
        sys.stderr.write("FAIL: UART log missing or empty\n")
        return 1

    boot_lines = [ln for ln in lines if ln.startswith("[open-fw] boot")]
    status_lines = [ln for ln in lines if ln.startswith("[open-fw] t=")]
    reject_lines = [ln for ln in lines if "signature rejected" in ln]

    if EXPECT == "unsigned":
        expect(any("sig=unsigned" in ln for ln in boot_lines), "missing boot sig=unsigned")
        expect(status_lines, "missing status lines for unsigned build")
    elif EXPECT == "reject":
        expect(any("sig=invalid" in ln for ln in boot_lines), "missing boot sig=invalid")
        expect(reject_lines, "missing signature rejected line")
        expect(not status_lines, "status lines present despite rejection")
    else:
        sys.stderr.write(f"unknown BC280_EXPECT_SIG={EXPECT}\n")
        return 1

    print(f"PASS: release integrity checks ({EXPECT})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
