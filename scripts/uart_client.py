#!/usr/bin/env python3
"""
Host CLI entrypoint for the BC280 UART debug protocol.

This wrapper re-exports the Renode UART client so host usage doesn't depend
on the scripts/renode path.
"""

import os
import sys


def _find_renode_client() -> str:
    here = os.path.abspath(os.path.dirname(__file__))
    return os.path.join(here, "renode")


def main() -> int:
    renode_dir = _find_renode_client()
    if renode_dir not in sys.path:
        sys.path.insert(0, renode_dir)
    try:
        from uart_client import main as renode_main
    except Exception as exc:
        raise SystemExit(f"Failed to load renode uart_client: {exc}") from exc
    return int(renode_main())


if __name__ == "__main__":
    raise SystemExit(main())
