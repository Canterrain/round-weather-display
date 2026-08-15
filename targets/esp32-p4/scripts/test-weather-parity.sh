#!/usr/bin/env bash
set -euo pipefail

# -----------------------------------------------------------------------------
# Runs the ESP32-P4 target's C ports of the forecast heuristic and location
# scorer against the exact same JSON fixtures the Pi/JS implementations are
# tested against (shared/test-data/). Compiles natively with plain `cc` --
# no ESP-IDF toolchain needed, since main/forecast_representative.c and
# main/location_scoring.c are deliberately free of ESP-IDF dependencies.
#
# This is the parity check: if you change shared/logic/forecast-representative.js
# or shared/logic/open-meteo-location.js (or the corresponding C ports),
# run this alongside `npm run test:forecast` / `npm run test:location` and
# make sure both sides still agree.
# -----------------------------------------------------------------------------

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
REPO_ROOT="$(cd "$PROJECT_DIR/../.." && pwd)"

CJSON_DIR="$REPO_ROOT/.esp-idf/esp-idf-v5.5.5/components/json/cJSON"
if [[ ! -f "$CJSON_DIR/cJSON.c" ]]; then
  echo "ERROR: cJSON not found at $CJSON_DIR"
  echo "This test harness reuses the cJSON copy vendored in ESP-IDF rather than"
  echo "vendoring a second copy -- install ESP-IDF first (targets/esp32-p4/scripts/setup.sh)."
  exit 1
fi

BUILD_DIR="$(mktemp -d)"
trap 'rm -rf "$BUILD_DIR"' EXIT

CC_EXPLICIT="${CC:-}"
CC="${CC:-cc}"
COMMON_FLAGS=(-std=c11 -Wall -Wextra -I "$PROJECT_DIR/main" -I "$CJSON_DIR")

# On macOS, a broken `xcode-select` state can break both the default `cc`
# binary itself and its ability to locate the SDK (-isysroot) -- a known,
# unrelated system misconfiguration, not specific to this project. Work
# around both automatically when CC wasn't explicitly set; leave an
# explicit CC alone (if you set it yourself, you want exactly that, broken
# or not).
if [[ "$(uname -s)" == "Darwin" ]]; then
  if [[ -z "$CC_EXPLICIT" ]] && ! "$CC" --version >/dev/null 2>&1; then
    FALLBACK_CLANG="/Library/Developer/CommandLineTools/usr/bin/clang"
    if [[ -x "$FALLBACK_CLANG" ]]; then
      echo "NOTE: '$CC' isn't working here (likely a broken 'xcode-select' state, unrelated"
      echo "to this project) -- using $FALLBACK_CLANG instead. 'sudo xcode-select --reset'"
      echo "fixes this system-wide if you want plain 'cc' to work again."
      CC="$FALLBACK_CLANG"
    fi
  fi

  SDK_PATH="$(xcrun --sdk macosx --show-sdk-path 2>/dev/null || true)"
  if [[ -z "$SDK_PATH" ]]; then
    SDK_PATH="/Library/Developer/CommandLineTools/SDKs/MacOSX.sdk"
  fi
  if [[ -d "$SDK_PATH" ]]; then
    COMMON_FLAGS+=(-isysroot "$SDK_PATH")
  fi
fi

echo "Building forecast parity test..."
"$CC" "${COMMON_FLAGS[@]}" \
  "$PROJECT_DIR/tests/test_forecast_representative.c" \
  "$PROJECT_DIR/main/forecast_representative.c" \
  "$CJSON_DIR/cJSON.c" \
  -o "$BUILD_DIR/test_forecast_representative"

echo "Building location parity test..."
"$CC" "${COMMON_FLAGS[@]}" \
  "$PROJECT_DIR/tests/test_location_scoring.c" \
  "$PROJECT_DIR/main/location_scoring.c" \
  "$CJSON_DIR/cJSON.c" \
  -o "$BUILD_DIR/test_location_scoring"

echo ""
echo "--- Forecast representative-code parity ---"
forecast_status=0
"$BUILD_DIR/test_forecast_representative" "$REPO_ROOT/shared/test-data/forecast-representative-cases.json" || forecast_status=$?

echo ""
echo "--- Location geocoding-scorer parity ---"
location_status=0
"$BUILD_DIR/test_location_scoring" "$REPO_ROOT/shared/test-data/location-resolution-cases.json" || location_status=$?

echo ""
if [[ "$forecast_status" -ne 0 || "$location_status" -ne 0 ]]; then
  echo "PARITY CHECK FAILED: the ESP32-P4 C port disagrees with the JS implementation"
  echo "on at least one fixture case. Fix the C port (or the JS, or the fixture,"
  echo "whichever is wrong) before shipping -- this is exactly the drift this"
  echo "test exists to catch before users see it."
  exit 1
fi

echo "All parity checks passed."
