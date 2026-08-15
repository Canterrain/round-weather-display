#!/usr/bin/env bash
set -euo pipefail

# -----------------------------------------------------------------------------
# ESP32-P4 target setup / update
#
# Safe to re-run: pulls the latest source, installs ESP-IDF if it isn't
# present yet, then builds and flashes. This is both the first-time setup
# path and the update path for this target -- there is no wireless OTA yet,
# so "update" means re-run this script with the board plugged in over USB.
# -----------------------------------------------------------------------------

echo "-------------------------------"
echo "ESP32-P4 Target Setup"
echo "-------------------------------"

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../../.." && pwd)"
IDF_ROOT="$REPO_ROOT/.esp-idf/esp-idf-v5.5.5"
IDF_TOOLS_PATH="$REPO_ROOT/.esp-idf/tools"
IDF_VERSION_TAG="v5.5.5"

cd "$REPO_ROOT"

# -----------------------------------------------------------------------------
# Pull latest source (this is the "update" step)
# -----------------------------------------------------------------------------
if git rev-parse --is-inside-work-tree >/dev/null 2>&1; then
  if [[ -n "$(git status --porcelain)" ]]; then
    echo "NOTE: You have uncommitted local changes, skipping 'git pull' so nothing"
    echo "gets clobbered. Commit or stash your changes and re-run this script to"
    echo "pick up the latest source before building."
  else
    echo "Pulling latest source..."
    git pull
  fi
else
  echo "NOTE: Not inside a git checkout, skipping the update pull."
fi

# -----------------------------------------------------------------------------
# Install ESP-IDF if it isn't already present at the pinned path
# -----------------------------------------------------------------------------
if [[ ! -d "$IDF_ROOT" ]]; then
  echo "ESP-IDF $IDF_VERSION_TAG not found at:"
  echo "  $IDF_ROOT"
  echo "Installing it now. This clones the ESP-IDF toolchain (several GB) and"
  echo "only needs to happen once -- it will take a while on the first run."
  mkdir -p "$REPO_ROOT/.esp-idf"
  git clone --branch "$IDF_VERSION_TAG" --recursive \
    https://github.com/espressif/esp-idf.git "$IDF_ROOT"
  IDF_TOOLS_PATH="$IDF_TOOLS_PATH" "$IDF_ROOT/install.sh" esp32p4
else
  echo "ESP-IDF $IDF_VERSION_TAG already installed at:"
  echo "  $IDF_ROOT"
fi

# -----------------------------------------------------------------------------
# Activate ESP-IDF environment in this shell
# -----------------------------------------------------------------------------
echo "Activating ESP-IDF environment..."
# shellcheck source=/dev/null
source "$SCRIPT_DIR/activate-idf.sh"

# -----------------------------------------------------------------------------
# Set target and build
# -----------------------------------------------------------------------------
"$SCRIPT_DIR/set-target.sh"

echo "Building firmware..."
"$SCRIPT_DIR/build.sh"

# -----------------------------------------------------------------------------
# Detect serial port
# -----------------------------------------------------------------------------
PORT="${WEATHER_ESP32_PORT:-}"
if [[ -z "$PORT" ]]; then
  shopt -s nullglob
  candidates=(/dev/cu.usbmodem* /dev/cu.usbserial* /dev/ttyACM* /dev/ttyUSB*)
  shopt -u nullglob

  if [[ ${#candidates[@]} -eq 1 ]]; then
    PORT="${candidates[0]}"
    echo "Found one board on: $PORT"
  elif [[ ${#candidates[@]} -eq 0 ]]; then
    echo "ERROR: No serial device found."
    echo "Plug in the ESP32-P4 board over USB and re-run this script."
    echo "(Or set WEATHER_ESP32_PORT=/dev/tty... to skip detection.)"
    exit 1
  else
    echo "Multiple serial devices found:"
    select choice in "${candidates[@]}"; do
      if [[ -n "$choice" ]]; then
        PORT="$choice"
        break
      fi
    done
  fi
fi

# -----------------------------------------------------------------------------
# Flash
# -----------------------------------------------------------------------------
echo "Flashing $PORT..."
"$SCRIPT_DIR/flash.sh" "$PORT"

echo "---------------------------------------"
echo " ESP32-P4 setup complete!"
echo "---------------------------------------"
echo "Flashed via: $PORT"
echo "Re-run this script any time to pick up updates from GitHub and reflash."
echo ""

read -r -p "Open the serial monitor now? (y/N) [N]: " openMonitor
openMonitor="${openMonitor:-N}"
case "$openMonitor" in
  Y|y) "$SCRIPT_DIR/monitor.sh" "$PORT" ;;
  *) echo "Skipping monitor. Run: targets/esp32-p4/scripts/monitor.sh $PORT" ;;
esac
