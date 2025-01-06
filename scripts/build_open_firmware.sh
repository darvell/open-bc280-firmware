#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd -- "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT/open-firmware"

echo "[build] Building open-firmware..."
make

BIN="$ROOT/open-firmware/build/open_firmware.bin"
if [[ -f "$BIN" ]]; then
  printf "[build] OK -> %s (%s bytes)\n" "$BIN" "$(stat -f%z "$BIN" 2>/dev/null || stat -c%s "$BIN")"
else
  echo "[build] build/open_firmware.bin not produced" >&2
  exit 1
fi
