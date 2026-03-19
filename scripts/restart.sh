#!/usr/bin/env bash
set -euo pipefail

APP_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
RWC="$APP_DIR/scripts/rwc.sh"
STOP="$APP_DIR/scripts/stop.sh"

APP_NAME="round-weather-display"
LOG_FILE="/tmp/${APP_NAME}-restart.log"

echo "[restart] Stopping..."
bash "$STOP"

sleep 1

echo "[restart] Starting..."
nohup bash "$RWC" >"$LOG_FILE" 2>&1 & disown

echo "[restart] Done. Log: $LOG_FILE"
