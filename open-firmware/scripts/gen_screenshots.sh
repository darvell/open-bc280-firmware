#!/bin/bash
# Generate UI screenshots for all pages using the host simulator.
# Outputs PNG files to docs/examples/
#
# Requirements:
#   - meson/ninja build configured
#   - ImageMagick (convert command) for PPM → PNG
#
# Usage: ./scripts/gen_screenshots.sh [builddir]
#   builddir defaults to 'build-host' if not specified

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
FIRMWARE_DIR="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="${1:-$FIRMWARE_DIR/build-host}"
OUT_DIR="$FIRMWARE_DIR/docs/examples"
LCD_DIR="$FIRMWARE_DIR/tests/host/lcd_out"

# Page names matching UI_PAGE_* enum order
PAGES=(
    "dashboard"        # 0
    "engineer_raw"     # 1
    "engineer_power"   # 2
    "focus"            # 3
    "graphs"           # 4
    "trip"             # 5
    "profiles"         # 6
    "settings"         # 7
    "cruise"           # 8
    "battery"          # 9
    "thermal"          # 10
    "diagnostics"      # 11
    "bus"              # 12
    "capture"          # 13
    "alerts"           # 14
    "tune"             # 15
    "ambient"          # 16
    "about"            # 17
)

echo "=== UI Screenshot Generator ==="
echo "Firmware dir: $FIRMWARE_DIR"
echo "Build dir:    $BUILD_DIR"
echo "Output dir:   $OUT_DIR"

# Check for ImageMagick (prefer magick, fall back to convert)
if command -v magick &> /dev/null; then
    IMG_CONVERT="magick"
elif command -v convert &> /dev/null; then
    IMG_CONVERT="convert"
else
    echo "Error: ImageMagick not found"
    echo "Install with: brew install imagemagick (macOS) or apt install imagemagick (Linux)"
    exit 1
fi

# Configure meson if needed
if [ ! -f "$BUILD_DIR/build.ninja" ]; then
    echo "Configuring meson build..."
    meson setup "$BUILD_DIR" "$FIRMWARE_DIR" --buildtype=debug
fi

# Build host simulator
echo "Building host_sim..."
ninja -C "$BUILD_DIR" tests/host/host_sim

# Create output directories
mkdir -p "$OUT_DIR"
mkdir -p "$LCD_DIR"

# Run simulator for each page
HOST_SIM="$BUILD_DIR/tests/host/host_sim"
if [ ! -x "$HOST_SIM" ]; then
    echo "Error: host_sim not found at $HOST_SIM"
    exit 1
fi

echo ""
echo "Generating screenshots..."
for i in "${!PAGES[@]}"; do
    PAGE_NAME="${PAGES[$i]}"
    PPM_FILE="$LCD_DIR/host_lcd_latest.ppm"
    PNG_FILE="$OUT_DIR/${PAGE_NAME}_screen.png"

    echo -n "  Page $i (${PAGE_NAME})... "

    # Run simulator with forced page, 30 steps to let UI stabilize
    BC280_SIM_FORCE_PAGE="$i" \
    BC280_SIM_STEPS=30 \
    BC280_SIM_DT_MS=50 \
    UI_LCD_OUTDIR="$LCD_DIR" \
    "$HOST_SIM" > /dev/null 2>&1 || true

    if [ -f "$PPM_FILE" ]; then
        # Convert PPM to PNG (2x scale for visibility)
        $IMG_CONVERT "$PPM_FILE" -scale 200% "$PNG_FILE"
        echo "OK → ${PAGE_NAME}_screen.png"
    else
        echo "FAILED (no PPM output)"
    fi
done

echo ""
echo "=== Screenshot generation complete ==="
echo "Output: $OUT_DIR/"
ls -la "$OUT_DIR"/*.png 2>/dev/null || echo "(no screenshots generated)"
