#!/usr/bin/env bash
set -euo pipefail

APP_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
RWC="$APP_DIR/scripts/rwc.sh"
STOP="$APP_DIR/scripts/stop.sh"

APP_NAME="round-weather-display"
LOG_FILE="/tmp/${APP_NAME}-start.log"

echo "[start] Starting $APP_NAME..."

# If pm2 exists AND it has our process, prefer pm2.
if command -v pm2 >/dev/null 2>&1; then
  if pm2 jlist 2>/dev/null | grep -q "\"name\":\"$APP_NAME\""; then
    pm2 start "$APP_NAME" >/dev/null 2>&1 || pm2 restart "$APP_NAME" >/dev/null 2>&1 || true
    echo "[start] Started via PM2."
    exit 0
  fi
fi

# Otherwise (Wayland/labwc path or fresh install), run the launcher directly.
# Stop any stray copies first (safe no-op if nothing is running).
bash "$STOP" >/dev/null 2>&1 || true

nohup bash "$RWC" >"$LOG_FILE" 2>&1 & disown
echo "[start] Started via rwc.sh. Log: $LOG_FILE"
