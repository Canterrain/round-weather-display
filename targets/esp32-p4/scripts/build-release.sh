#!/usr/bin/env bash
set -euo pipefail

# -----------------------------------------------------------------------------
# Builds the ESP32-P4 firmware and copies the release artifacts into docs/,
# which GitHub Pages serves as the browser-flashing page
# (https://<org>.github.io/round-weather-display/). Run this after any
# firmware change you want end users to be able to install via that page --
# it is NOT run automatically, so the hosted page only ever has whatever was
# last copied here.
#
# Bump the "version" field in docs/firmware/manifest.json yourself when you
# do this for a real release; this script doesn't guess at versioning.
# -----------------------------------------------------------------------------

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
REPO_ROOT="$(cd "$PROJECT_DIR/../.." && pwd)"
DOCS_FIRMWARE_DIR="$REPO_ROOT/docs/firmware"

source "$SCRIPT_DIR/activate-idf.sh"
require_idf() {
  if ! command -v idf.py >/dev/null 2>&1; then
    echo "ERROR: idf.py was not found in PATH."
    exit 1
  fi
}
require_idf

echo "Building firmware..."
idf.py -C "$PROJECT_DIR" build

mkdir -p "$DOCS_FIRMWARE_DIR"
cp "$PROJECT_DIR/build/bootloader/bootloader.bin" "$DOCS_FIRMWARE_DIR/bootloader.bin"
cp "$PROJECT_DIR/build/partition_table/partition-table.bin" "$DOCS_FIRMWARE_DIR/partition-table.bin"
cp "$PROJECT_DIR/build/round_weather_display_esp32_p4.bin" "$DOCS_FIRMWARE_DIR/app.bin"

echo ""
echo "Copied release artifacts to $DOCS_FIRMWARE_DIR"
echo "Don't forget to update the \"version\" field in $DOCS_FIRMWARE_DIR/manifest.json"
echo "if this is a real release, then commit and push."
