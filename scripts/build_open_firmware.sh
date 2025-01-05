#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd -- "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

echo "[build] Building open-firmware..."
meson setup build --cross-file cross/arm-none-eabi-clang.txt || \
  meson setup --reconfigure build --cross-file cross/arm-none-eabi-clang.txt
ninja -C build

BIN="$ROOT/build/open_firmware.bin"
if [[ -f "$BIN" ]]; then
  printf "[build] OK -> %s (%s bytes)\n" "$BIN" "$(stat -f%z "$BIN" 2>/dev/null || stat -c%s "$BIN")"
else
  echo "[build] build/open_firmware.bin not produced" >&2
  exit 1
fi
