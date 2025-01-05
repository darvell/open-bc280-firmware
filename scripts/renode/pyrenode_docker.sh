#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
IMAGE_NAME=${PYRENODE_IMAGE:-bc280-pyrenode}
PLATFORM=${PYRENODE_PLATFORM:-}
TARBALL_URL=${PYRENODE_TARBALL_URL:-}

if [ -z "$PLATFORM" ]; then
  ARCH=$(uname -m)
  if [ "$ARCH" = "arm64" ] || [ "$ARCH" = "aarch64" ]; then
    PLATFORM=linux/arm64
  else
    PLATFORM=linux/amd64
  fi
fi

if [ -z "$TARBALL_URL" ]; then
  if [ "$PLATFORM" = "linux/arm64" ]; then
    TARBALL_URL="https://builds.renode.io/renode-latest.linux-arm64-portable-dotnet.tar.gz"
  else
    TARBALL_URL="https://builds.renode.io/renode-latest.linux-portable-dotnet.tar.gz"
  fi
fi

if ! docker image inspect "$IMAGE_NAME" >/dev/null 2>&1; then
  docker build --platform "$PLATFORM" \
    --build-arg RENODE_TARBALL_URL="$TARBALL_URL" \
    -t "$IMAGE_NAME" \
    -f "$ROOT_DIR/scripts/renode/pyrenode_docker.Dockerfile" \
    "$ROOT_DIR"
fi

if [ $# -eq 0 ]; then
  set -- python scripts/renode/pyrenode_smoke.py
fi

RUN_CMD=""
for arg in "$@"; do
  RUN_CMD+=$(printf "%q " "$arg")
done

docker run --rm -it --platform "$PLATFORM" \
  -e BC280_SKIP_BUILD="${BC280_SKIP_BUILD:-}" \
  -e BC280_OPEN_FW_BIN="${BC280_OPEN_FW_BIN:-}" \
  -e BC280_COMBINED_FW_BIN="${BC280_COMBINED_FW_BIN:-}" \
  -e BC280_MAX_RUNTIME_S="${BC280_MAX_RUNTIME_S:-}" \
  -e BC280_MAX_STARTUP_S="${BC280_MAX_STARTUP_S:-}" \
  -e BC280_TIMEOUT_SCALE="${BC280_TIMEOUT_SCALE:-}" \
  -e BC280_MIN_SLEEP_S="${BC280_MIN_SLEEP_S:-}" \
  -e BC280_OTA_FAST="${BC280_OTA_FAST:-}" \
  -e BC280_OTA_CHUNK="${BC280_OTA_CHUNK:-}" \
  -e BC280_OTA_THROTTLE_S="${BC280_OTA_THROTTLE_S:-}" \
  -e BC280_OTA_BLOCK_SLEEP_S="${BC280_OTA_BLOCK_SLEEP_S:-}" \
  -e BC280_BOOT_CMD_TRIES="${BC280_BOOT_CMD_TRIES:-}" \
  -e BC280_BOOT_CMD_WAIT_S="${BC280_BOOT_CMD_WAIT_S:-}" \
  -e BC280_SKIP_APP_LOOP="${BC280_SKIP_APP_LOOP:-}" \
  -e BC280_SKIP_UART1_READY="${BC280_SKIP_UART1_READY:-}" \
  -e BC280_OTA_MODE="${BC280_OTA_MODE:-}" \
  -e BC280_UART1_SOFT_ENQUEUE="${BC280_UART1_SOFT_ENQUEUE:-}" \
  -e BC280_APP_BLE_UART="${BC280_APP_BLE_UART:-}" \
  -e BC280_HOOK_VERBOSE="${BC280_HOOK_VERBOSE:-}" \
  -e BC280_SPI_WIP_FAST="${BC280_SPI_WIP_FAST:-}" \
  -e BC280_FORCE_APP_BOOTFLAG="${BC280_FORCE_APP_BOOTFLAG:-}" \
  -e BC280_CLEAR_BOOTFLAG_BEFORE_APP="${BC280_CLEAR_BOOTFLAG_BEFORE_APP:-}" \
  -e BC280_FORCE_APP_JUMP="${BC280_FORCE_APP_JUMP:-}" \
  -e BC280_FORCE_APP_MAIN="${BC280_FORCE_APP_MAIN:-}" \
  -e BC280_FAST_FLASH_READ="${BC280_FAST_FLASH_READ:-}" \
  -e BC280_FAST_BOOTFLAG_WRITE="${BC280_FAST_BOOTFLAG_WRITE:-}" \
  -e BC280_FAST_FLOW="${BC280_FAST_FLOW:-}" \
  -e BC280_OUTDIR_BASE="${BC280_OUTDIR_BASE:-}" \
  -v "$ROOT_DIR":/work \
  -w /work \
  "$IMAGE_NAME" bash -lc "RENODE_REAL=\$(readlink -f /opt/renode/renode); export PYRENODE_BIN=\$RENODE_REAL; export RENODE_BIN=\$RENODE_REAL; python /work/scripts/renode/pyrenode_prep.py && exec $RUN_CMD"
