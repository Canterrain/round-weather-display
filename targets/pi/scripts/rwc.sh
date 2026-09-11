#!/usr/bin/env bash
set -euo pipefail

APP_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$APP_DIR" || exit 1

SESSION="${XDG_SESSION_TYPE:-}"
IS_WAYLAND="false"
if [[ "$SESSION" == "wayland" || -n "${WAYLAND_DISPLAY:-}" ]]; then
  IS_WAYLAND="true"
fi

# Give the session a moment to settle (harmless on both)
sleep 2
export PORT="${PORT:-3000}"

# X11-only: hide cursor with unclutter
if [[ "$IS_WAYLAND" == "false" ]]; then
  export DISPLAY="${DISPLAY:-:0}"
  if command -v unclutter-xfixes >/dev/null 2>&1; then
    unclutter-xfixes --timeout 0 --jitter 0 --ignore-scrolling &
  fi
else
  # Wayland: nudge Electron to use Ozone automatically (Wayland where possible)
  export ELECTRON_OZONE_PLATFORM_HINT=auto
fi

# Start the Express server
node --network-family-autoselection-attempt-timeout=500 server.js &

# Wait for server to be ready (max ~30s)
for _ in {1..30}; do
  if curl -fsS "http://localhost:${PORT}/weather" >/dev/null 2>&1; then
    break
  fi
  sleep 1
done

# --disable-gpu: the Pi Zero (2) W has no working GPU driver for Chromium's
# Wayland/EGL path -- confirmed via live logs on a Zero 2 W showing
# "Requested GLES version (3.0) is greater than max supported (2, 0)" and
# "VK_ERROR_INCOMPATIBLE_DRIVER", repeatedly attempting and failing GPU
# initialization before falling back to software rendering anyway. Skipping
# straight to a clean software path measured via Chrome DevTools Protocol
# event timing (touch-to-next-paint) at roughly a 2-3x improvement in swipe
# responsiveness (2100-3700ms -> 500-1100ms for the same interactions).
# Safe here since the UI only uses CSS/SVG, no WebGL/canvas/video that would
# actually need GPU acceleration.
#
# Also applied on a Pi 3 (Model B/B+): same VideoCore IV GPU generation as
# the Zero 2 W (BCM2837 vs. BCM2710A1), so it's reasoned to hit the same
# broken EGL/Vulkan path -- reasoned from hardware generation, not verified
# on real Pi 3 hardware (no unit available to test against).
#
# Not applied on a Pi 4/5 -- those have real, working GPU acceleration
# (VideoCore VI/VII), and disabling it there would trade a working fast path
# for a slow one. Untested on 4/5 (no hardware to verify against), so this
# stays scoped to the boards it's actually proven or reasoned to need it on,
# rather than risking a regression elsewhere.
ELECTRON_ARGS=()
PI_MODEL="$(tr -d '\0' < /proc/device-tree/model 2>/dev/null || true)"
if [[ "$PI_MODEL" == *"Zero"* || "$PI_MODEL" == *"Pi 3"* ]]; then
  ELECTRON_ARGS+=(--disable-gpu)
fi

# Launch Electron
exec ./node_modules/.bin/electron "${ELECTRON_ARGS[@]}" .
