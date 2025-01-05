#!/usr/bin/env python3
"""Smoke-check pyrenode3 + portable Renode runtime."""

from pyrenode3.wrappers import Emulation, Monitor


def main() -> int:
    try:
        _ = Emulation()
        m = Monitor()
        out, err = m.execute("version")
        print("pyrenode3 ok")
        if out:
            print(out.strip())
        if err:
            print(err.strip())
        return 0
    except Exception as exc:
        print("pyrenode3 err", type(exc).__name__, exc)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
