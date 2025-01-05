#!/usr/bin/env python3
"""Ensure Renode coreclr extraction has hostfxr/hostpolicy available."""

import json
import os
import re
import subprocess
from pathlib import Path


def find_extraction_path(renode_bin: str) -> Path | None:
    env = os.environ.copy()
    env["COREHOST_TRACE"] = "1"
    env["COREHOST_TRACEFILE"] = ""
    try:
        out = subprocess.check_output([renode_bin, "--version"], stderr=subprocess.STDOUT, text=True, env=env)
    except subprocess.CalledProcessError as exc:
        out = exc.output or ""
    for line in out.splitlines():
        if "extracted to" not in line:
            continue
        match = re.search(r"will be extracted to \[(.*)\] directory", line)
        if match:
            return Path(match.group(1))
    match = re.search(r"will be extracted to \[(.*)\] directory", out)
    if match:
        return Path(match.group(1))
    return None


def ensure_lib(target: Path, src: Path) -> None:
    if target.exists() or not src.exists():
        return
    try:
        target.parent.mkdir(parents=True, exist_ok=True)
        target.symlink_to(src)
    except Exception:
        try:
            target.write_bytes(src.read_bytes())
        except Exception:
            pass


def discover_renode_dir(renode_path: Path) -> Path | None:
    try:
        candidate = renode_path.resolve().parent
    except Exception:
        candidate = renode_path.parent
    if (candidate / "libhostfxr.so").exists():
        return candidate
    # Fallback: look for renode_* directories near the symlink.
    for root in (renode_path.parent, renode_path.parent.parent):
        if not root.exists():
            continue
        for subdir in root.glob("renode*"):
            if (subdir / "libhostfxr.so").exists():
                return subdir
    return None


def find_runtime_version(extract_dir: Path) -> str | None:
    deps = extract_dir / "Renode.deps.json"
    if deps.exists():
        try:
            data = json.loads(deps.read_text())
            target = data["targets"][data["runtimeTarget"]["name"]]
            for lib in target:
                if lib.startswith("runtimepack.Microsoft.NETCore.App.Runtime."):
                    return lib.split("/")[1]
        except Exception:
            pass
    return None


def main() -> int:
    renode_bin = os.environ.get("PYRENODE_BIN", "/opt/renode/renode")
    renode_path = Path(renode_bin)
    if not renode_path.exists():
        return 1
    extract_dir = find_extraction_path(renode_bin)
    if not extract_dir:
        return 0
    renode_dir = discover_renode_dir(renode_path)
    if not renode_dir:
        return 0
    runtime_version = find_runtime_version(extract_dir)
    libhostfxr = renode_dir / "libhostfxr.so"
    libhostpolicy = renode_dir / "libhostpolicy.so"

    ensure_lib(extract_dir / "libhostfxr.so", libhostfxr)
    ensure_lib(extract_dir / "libhostpolicy.so", libhostpolicy)

    if runtime_version:
        ensure_lib(
            extract_dir / "host" / "fxr" / runtime_version / "libhostfxr.so",
            libhostfxr,
        )
        ensure_lib(
            extract_dir / "shared" / "Microsoft.NETCore.App" / runtime_version / "libhostpolicy.so",
            libhostpolicy,
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
