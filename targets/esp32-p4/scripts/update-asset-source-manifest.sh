#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
ASSET_DIR="$PROJECT_DIR/main/assets"
MANIFEST_PATH="$ASSET_DIR/generated_sources.cmake"
TMP_PATH="$(mktemp)"

cleanup() {
  rm -f "$TMP_PATH"
}
trap cleanup EXIT

mkdir -p "$ASSET_DIR"

{
  echo "set(APP_ASSET_SOURCES"
  while IFS= read -r asset_name; do
    printf '  "${CMAKE_CURRENT_LIST_DIR}/%s"\n' "$asset_name"
  done < <(cd "$ASSET_DIR" && find . -maxdepth 1 -name '*.c' -print | sed 's|^\./||' | LC_ALL=C sort)
  echo ")"
} > "$TMP_PATH"

mv "$TMP_PATH" "$MANIFEST_PATH"
echo "Asset manifest updated: $MANIFEST_PATH"
