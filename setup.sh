#!/usr/bin/env bash
set -euo pipefail

# -----------------------------------------------------------------------------
# Round Clock Weather Display Setup
#
# Supports:
#   - Raspberry Pi OS Bookworm (X11 or Wayland)
#   - Raspberry Pi OS Trixie (Wayland/labwc default)
#
# Branch behavior:
#   - Defaults to main.
#   - Override with:
#       WEATHER_BRANCH=<branch> bash setup.sh
#   - Optional:
#       WEATHER_REPO_URL=<repo url>
#   - Optional:
#       WEATHER_HARDWARE_TARGET=pi bash setup.sh
# -----------------------------------------------------------------------------

echo "-------------------------------"
echo "Round Clock Weather Display Setup"
echo "-------------------------------"

# -----------------------------------------------------------------------------
# Guardrails
# -----------------------------------------------------------------------------
if [[ "${EUID}" -eq 0 ]]; then
  echo "ERROR: Do not run this script with sudo."
  echo "Run: bash setup.sh"
  exit 1
fi

ARCH="$(uname -m)"
if [[ "$ARCH" == "armv7l" ]]; then
  echo "WARNING: 32-bit Raspberry Pi OS detected (armv7l)."
  echo "Electron installs can fail on 32-bit. Strongly recommend 64-bit Raspberry Pi OS (aarch64)."
  echo "Continuing anyway..."
fi

# -----------------------------------------------------------------------------
# Branch selection
#
# Defaults to main.
# Override with:
#   WEATHER_BRANCH=<branch> bash setup.sh
#
# Optional:
#   WEATHER_REPO_URL=<repo url>
# -----------------------------------------------------------------------------
SETUP_BRANCH="main"
REPO_BRANCH="${WEATHER_BRANCH:-$SETUP_BRANCH}"
DEFAULT_REPO_URL="https://github.com/Canterrain/round-weather-display.git"
REPO_URL="${WEATHER_REPO_URL:-$DEFAULT_REPO_URL}"

# -----------------------------------------------------------------------------
# Hardware target
# -----------------------------------------------------------------------------
HARDWARE_TARGET="${WEATHER_HARDWARE_TARGET:-}"
if [[ -z "$HARDWARE_TARGET" ]]; then
  echo "Hardware target:"
  echo "1. Raspberry Pi + round HDMI display"
  echo "2. ESP32-P4 3.4C round display"
  read -r -p "Choose 1 or 2 [1]: " hardwareChoice
  hardwareChoice="${hardwareChoice:-1}"

  case "$hardwareChoice" in
    2) HARDWARE_TARGET="esp32-p4" ;;
    *) HARDWARE_TARGET="pi" ;;
  esac
fi

if [[ "$HARDWARE_TARGET" != "pi" ]]; then
  ESP32_SETUP_SCRIPT="$(cd "$(dirname "$0")" && pwd)/targets/esp32-p4/scripts/setup.sh"
  if [[ ! -f "$ESP32_SETUP_SCRIPT" ]]; then
    echo "ERROR: Expected the ESP32-P4 setup script at:"
    echo "  $ESP32_SETUP_SCRIPT"
    echo "This flow needs a full repo checkout (a dev toolchain and USB access to the"
    echo "board), not the fresh headless install setup.sh does for the Pi. Clone the"
    echo "repo first, then run: targets/esp32-p4/scripts/setup.sh"
    exit 1
  fi
  exec bash "$ESP32_SETUP_SCRIPT"
fi

# -----------------------------------------------------------------------------
# Path constants (needed early so we can load the previous install's config
# as prompt defaults, and reused later for the backup-before-refresh step)
# -----------------------------------------------------------------------------
APP_NAME="round-weather-display"
TARGET_DIR="$HOME/$APP_NAME"
PI_TARGET_DIR="$TARGET_DIR/targets/pi"
BACKUP_DIR="$HOME/${APP_NAME}-backups"
SETUP_TMP="/tmp/${APP_NAME}-setup.sh"

# -----------------------------------------------------------------------------
# Load previous config.json (if this is a re-run/update) so prompts below can
# default to what's already configured instead of forcing full re-entry.
# -----------------------------------------------------------------------------
prevConfigJson=""
if [[ -f "$PI_TARGET_DIR/config.json" ]]; then
  prevConfigJson="$PI_TARGET_DIR/config.json"
  echo "Found existing config at $PI_TARGET_DIR/config.json -- reusing its values as defaults."
fi

read_prev() {
  local key="$1"
  local default="$2"
  if [[ -z "$prevConfigJson" ]]; then
    printf '%s' "$default"
    return
  fi
  python3 - "$prevConfigJson" "$key" "$default" <<'PYEOF'
import json, sys
path, key, default = sys.argv[1], sys.argv[2], sys.argv[3]
try:
    with open(path) as f:
        data = json.load(f)
    value = data.get(key, default)
    if value is None:
        value = default
    if isinstance(value, bool):
        value = 'true' if value else 'false'
    print(value)
except Exception:
    print(default)
PYEOF
}

prevCity="$(read_prev location "")"
prevRoomName="$(read_prev roomName "Clock")"
prevTimeFormat="$(read_prev timeFormat "12")"
prevUnits="$(read_prev units "imperial")"
prevDefaultClockFace="$(read_prev defaultClockFace "digital")"
prevMessageSharing="$(read_prev messageSharing "single")"
prevNightShift="$(read_prev nightShift "false")"
prevLeadingZero12h="$(read_prev leadingZero12h "true")"

prevDefaultClockFaceChoice="2"
[[ "$prevDefaultClockFace" == "analog" ]] && prevDefaultClockFaceChoice="1"

prevMessageSharingChoice="1"
[[ "$prevMessageSharing" == "shared" ]] && prevMessageSharingChoice="2"

prevNightShiftChoice="N"
[[ "$prevNightShift" == "true" ]] && prevNightShiftChoice="Y"

prevLeadingZero12hChoice="Y"
[[ "$prevLeadingZero12h" == "false" ]] && prevLeadingZero12hChoice="N"

# -----------------------------------------------------------------------------
# Config prompts
# -----------------------------------------------------------------------------
read -r -p "Enter your city (e.g., Loveland,OH,US)${prevCity:+ [$prevCity]}: " city
city="${city:-$prevCity}"
read -r -p "Enter a room label for this clock (e.g., Kitchen, Office, Bedroom) [$prevRoomName]: " roomName
read -r -p "Choose time format (12 or 24) [$prevTimeFormat]: " timeFormat
read -r -p "Choose temperature units (imperial or metric) [$prevUnits]: " units
echo "Default clock face:"
echo "1. Analog"
echo "2. Digital"
read -r -p "Choose 1 or 2 [$prevDefaultClockFaceChoice]: " defaultClockFaceChoice
echo "Message sharing:"
echo "1. Just this clock"
echo "2. Shared with other clocks"
read -r -p "Choose 1 or 2 [$prevMessageSharingChoice]: " messageSharingChoice
read -r -p "Enable red nightshift mode, dim red text from 22:00 to 06:00? (y/N) [$prevNightShiftChoice]: " nightShiftChoice

leadingZero12h="true"
nightShift="false"
roomName="${roomName:-$prevRoomName}"
timeFormat="${timeFormat:-$prevTimeFormat}"
units="${units:-$prevUnits}"
defaultClockFaceChoice="${defaultClockFaceChoice:-$prevDefaultClockFaceChoice}"
messageSharingChoice="${messageSharingChoice:-$prevMessageSharingChoice}"
nightShiftChoice="${nightShiftChoice:-$prevNightShiftChoice}"

if [[ "$timeFormat" != "12" && "$timeFormat" != "24" ]]; then
  timeFormat="12"
fi
if [[ "$units" != "imperial" && "$units" != "metric" ]]; then
  units="imperial"
fi

if [[ "$messageSharingChoice" == "2" ]]; then
  messageSharing="shared"
else
  messageSharing="single"
fi

if [[ "$defaultClockFaceChoice" == "2" ]]; then
  defaultClockFace="digital"
else
  defaultClockFace="analog"
fi

case "$nightShiftChoice" in
  Y|y) nightShift="true" ;;
  *)   nightShift="false" ;;
esac

deviceId="$(
  printf '%s' "$roomName" \
    | tr '[:upper:]' '[:lower:]' \
    | sed -E 's/[^a-z0-9]+/-/g; s/^-+//; s/-+$//'
)"

if [[ -z "$deviceId" ]]; then
  deviceId="clock"
fi

deviceId="${deviceId}-clock"

if [[ "$timeFormat" == "12" ]]; then
  read -r -p "Show leading zero in 12-hour mode, 07:00 AM instead of 7:00 AM? (Y/n) [$prevLeadingZero12hChoice]: " lz
  lz="${lz:-$prevLeadingZero12hChoice}"
  case "$lz" in
    Y|y) leadingZero12h="true" ;;
    N|n) leadingZero12h="false" ;;
    *)   leadingZero12h="true" ;;
  esac
fi

# -----------------------------------------------------------------------------
# Detect session (Wayland vs X11)
# -----------------------------------------------------------------------------
detect_session_type() {
  if [[ "${XDG_SESSION_TYPE:-}" == "wayland" || -n "${WAYLAND_DISPLAY:-}" ]]; then
    echo "wayland"
    return
  fi
  if [[ "${XDG_SESSION_TYPE:-}" == "x11" || -n "${DISPLAY:-}" ]]; then
    echo "x11"
    return
  fi

  # If run over SSH, infer from OS codename
  if [[ -f /etc/os-release ]]; then
    # shellcheck disable=SC1091
    . /etc/os-release
    if [[ "${VERSION_CODENAME:-}" == "trixie" ]]; then
      echo "wayland"
      return
    fi
    if [[ "${VERSION_CODENAME:-}" == "bookworm" ]]; then
      # Bookworm can be Wayland or X11, but if we're in SSH with no GUI env,
      # assume X11 (your existing, known-good path).
      echo "x11"
      return
    fi
  fi

  echo "x11"
}

SESSION_TYPE="$(detect_session_type)"
echo "Detected session type: $SESSION_TYPE"

# -----------------------------------------------------------------------------
# Install system dependencies
# -----------------------------------------------------------------------------
echo "Installing system packages..."
sudo apt-get update
sudo apt-get install -y \
  git \
  python3 \
  python3-venv \
  curl \
  ca-certificates \
  gnupg \
  fontconfig \
  unzip \
  wlr-randr

# X11 packages only when needed
if [[ "$SESSION_TYPE" == "x11" ]]; then
  sudo apt-get install -y \
    xserver-xorg \
    xinit \
    x11-xserver-utils
fi

# Cursor hide dependencies
if [[ "$SESSION_TYPE" == "x11" ]]; then
  sudo apt-get remove -y unclutter || true
  sudo apt-get install -y unclutter-xfixes
else
  sudo apt-get install -y wtype
fi

# -----------------------------------------------------------------------------
# Node.js 20 LTS (NodeSource)
# -----------------------------------------------------------------------------
ensure_node20() {
  local major="0"
  if command -v node >/dev/null 2>&1; then
    major="$(node -p 'process.versions.node.split(".")[0]')"
  fi

  if [[ "$major" != "20" ]]; then
    echo "Installing Node.js 20 LTS..."
    sudo apt-get remove -y nodejs npm || true
    curl -fsSL https://deb.nodesource.com/setup_20.x | sudo -E bash -
    sudo apt-get install -y nodejs
  fi

  echo "Node: $(node -v)"
  echo "npm:  $(npm -v)"
}

ensure_node20

# Make npm downloads more resilient on flaky networks
npm config set fetch-retries 5 >/dev/null 2>&1 || true
npm config set fetch-retry-maxtimeout 120000 >/dev/null 2>&1 || true

# -----------------------------------------------------------------------------
# Backup existing config.json before refresh
# -----------------------------------------------------------------------------
if [[ -f "$PI_TARGET_DIR/config.json" ]]; then
  ts="$(date +%Y%m%d-%H%M%S)"
  mkdir -p "$BACKUP_DIR"
  cp -f "$PI_TARGET_DIR/config.json" "$BACKUP_DIR/config.json.$ts.bak"
  echo "Backed up existing config.json to: ~/${APP_NAME}-backups/config.json.$ts.bak"
fi

# -----------------------------------------------------------------------------
# Refresh install: wipe + clone latest
# Safe even if user ran setup from inside the folder
# -----------------------------------------------------------------------------
if [[ "$(pwd -P)" == "$TARGET_DIR"* ]]; then
  echo "Setup is running from inside $TARGET_DIR."
  echo "Re-launching from HOME so refresh can proceed safely..."
  cp -f "$0" "$SETUP_TMP"
  chmod +x "$SETUP_TMP"
  cd "$HOME"
  exec bash "$SETUP_TMP"
fi

if [[ -z "$REPO_URL" ]]; then
  echo "ERROR: No repository URL configured for refresh install."
  echo "Set WEATHER_REPO_URL to your fork URL when running setup.sh."
  echo 'Example: WEATHER_REPO_URL="https://github.com/<you>/<repo>.git" bash setup.sh'
  exit 1
fi

echo "Cloning $APP_NAME from configured repository..."
rm -rf "$TARGET_DIR"
git clone --branch "$REPO_BRANCH" --single-branch "$REPO_URL" "$TARGET_DIR"

if [[ ! -d "$PI_TARGET_DIR" ]]; then
  echo "ERROR: Expected Raspberry Pi target at $PI_TARGET_DIR"
  exit 1
fi

# -----------------------------------------------------------------------------
# Resolve city -> lat/lon/timezone via Open-Meteo Geocoding API
# -----------------------------------------------------------------------------
echo "Resolving location to latitude/longitude/timezone..."
geo_json=""
if ! geo_json="$(node "$TARGET_DIR/shared/scripts/resolve-location.js" "$city")"; then
  geo_json=""
fi

lat="$(echo "$geo_json" | python3 -c "import sys, json; s=sys.stdin.read().strip(); print(json.loads(s).get('lat','') if s else '')")"
lon="$(echo "$geo_json" | python3 -c "import sys, json; s=sys.stdin.read().strip(); print(json.loads(s).get('lon','') if s else '')")"
tz="$(echo "$geo_json" | python3 -c "import sys, json; s=sys.stdin.read().strip(); print(json.loads(s).get('timezone','auto') if s else 'auto')")"

if [[ -z "$lat" || -z "$lon" ]]; then
  echo "ERROR: Could not resolve lat/lon for '$city'."
  echo "Double-check the format (City,ST,CC) and try again."
  exit 1
fi

cat <<EOF > "$PI_TARGET_DIR/config.json"
{
  "location": "$city",
  "lat": $lat,
  "lon": $lon,
  "timezone": "$tz",
  "units": "$units",
  "deviceId": "$deviceId",
  "roomName": "$roomName",
  "messageSharing": "$messageSharing",
  "defaultClockFace": "$defaultClockFace",
  "timeFormat": "$timeFormat",
  "leadingZero12h": $leadingZero12h,
  "nightShift": $nightShift,
  "nightShiftStart": "22:00",
  "nightShiftEnd": "06:00",
  "thundersnowF": 34,
  "thundersnowC": 1,
  "recentSnowHours": 2,
  "recentSnowMm": 0,
  "recentPrecipMinutes": 60,
  "recentPrecipMm": 0,
  "recentSnowMm15": 0,
  "snowTempF": 34,
  "snowTempC": 1
}
EOF

# -----------------------------------------------------------------------------
# Node dependencies
# -----------------------------------------------------------------------------
echo "Installing Node dependencies..."
cd "$PI_TARGET_DIR"
if [[ -f package-lock.json ]]; then
  npm ci
else
  npm install
fi

# -----------------------------------------------------------------------------
# Ensure scripts are executable
# -----------------------------------------------------------------------------
chmod +x "$PI_TARGET_DIR/scripts/"*.sh 2>/dev/null || true
chmod +x "$PI_TARGET_DIR/rotate_display.sh" 2>/dev/null || true

# -----------------------------------------------------------------------------
# X11 rotation: systemd --user service that calls repo rotate_display.sh
# (Rewrite the unit every run so it stays in sync for everyone.)
# -----------------------------------------------------------------------------
setup_x11_rotation_service() {
  mkdir -p "$HOME/.config/systemd/user"

  cat <<'EOF' > "$HOME/.config/systemd/user/rotate-display.service"
[Unit]
Description=Rotate Display on Boot
After=graphical-session.target graphical.target

[Service]
Type=oneshot
Environment=DISPLAY=:0
Environment=XAUTHORITY=%h/.Xauthority
ExecStart=%h/round-weather-display/targets/pi/rotate_display.sh
TimeoutSec=120

[Install]
WantedBy=default.target
EOF

  systemctl --user daemon-reload
  systemctl --user enable rotate-display.service >/dev/null 2>&1 || true

  # Try once now (won't fail the install if it can't rotate yet)
  systemctl --user restart rotate-display.service >/dev/null 2>&1 || true
}

# -----------------------------------------------------------------------------
# Wayland helpers
# -----------------------------------------------------------------------------
create_wayland_helpers() {
  cat <<'EOF' > "$PI_TARGET_DIR/scripts/rotate_wayland.sh"
#!/usr/bin/env bash
set -euo pipefail

if ! command -v wlr-randr >/dev/null 2>&1; then
  exit 0
fi

out="$(wlr-randr | awk '
  /^[^ ]/ {o=$1}
  /Enabled: yes/ {print o; exit}
')"

if [[ -n "${out:-}" ]]; then
  wlr-randr --output "$out" --transform 90 || true
fi
EOF
  chmod +x "$PI_TARGET_DIR/scripts/rotate_wayland.sh"
}

# -----------------------------------------------------------------------------
# Autostart configuration
# -----------------------------------------------------------------------------
configure_labwc_wayland() {
  echo "Configuring labwc (Wayland) autostart, rotation, and cursor hiding..."

  mkdir -p "$HOME/.config/labwc"

  rc="$HOME/.config/labwc/rc.xml"
  if [[ -f "$rc" ]]; then
    ts="$(date +%Y%m%d-%H%M%S)"
    cp -f "$rc" "$rc.bak.$ts"
  else
    cat <<'XML' > "$rc"
<?xml version="1.0"?>
<openbox_config xmlns="http://openbox.org/3.4/rc">
</openbox_config>
XML
  fi

  if ! grep -q 'action name="HideCursor"' "$rc"; then
    tmp_rc="$(mktemp)"
    awk '
      BEGIN { inserted=0 }
      /<\/openbox_config>/ && inserted==0 {
        print ""
        print "  <keyboard>"
        print "    <keybind key=\"A-W-h\">"
        print "      <action name=\"HideCursor\" />"
        print "      <action name=\"WarpCursor\" x=\"-1\" y=\"-1\" />"
        print "    </keybind>"
        print "  </keyboard>"
        print ""
        inserted=1
      }
      { print }
    ' "$rc" > "$tmp_rc"
    mv "$tmp_rc" "$rc"
  fi

  aut="$HOME/.config/labwc/autostart"
  touch "$aut"

  add_line() {
    local line="$1"
    grep -Fqx "$line" "$aut" 2>/dev/null || echo "$line" >> "$aut"
  }

  add_line "bash \"$PI_TARGET_DIR/scripts/rotate_wayland.sh\" &"
  add_line "wtype -M alt -M logo h -m alt -m logo &"
  add_line "bash \"$PI_TARGET_DIR/scripts/rwc.sh\" &"

  echo "labwc configuration updated:"
  echo "  $rc"
  echo "  $aut"
}

configure_x11_pm2() {
  echo "Configuring X11 autostart via PM2..."

  echo "Installing PM2..."
  sudo npm install -g pm2

  pm2 start "$PI_TARGET_DIR/scripts/rwc.sh" --name "$APP_NAME" || true

  pm2StartupCmd="$(pm2 startup systemd -u "$USER" --hp "/home/$USER" | grep sudo || true)"
  if [[ -n "$pm2StartupCmd" ]]; then
    eval "$pm2StartupCmd"
  fi
  pm2 save
}

# -----------------------------------------------------------------------------
# Apply session-specific setup
# -----------------------------------------------------------------------------
if [[ "$SESSION_TYPE" == "wayland" ]]; then
  # Don't rely on PM2 boot services on labwc
  sudo systemctl disable pm2-"$USER" >/dev/null 2>&1 || true

  create_wayland_helpers
  configure_labwc_wayland
else
  setup_x11_rotation_service
  configure_x11_pm2
fi

# -----------------------------------------------------------------------------
# Done
# -----------------------------------------------------------------------------
echo "---------------------------------------"
echo " Setup complete!"
echo "---------------------------------------"
echo "Installed to: $TARGET_DIR"
echo "Hardware target: Raspberry Pi"
echo "Pi runtime path: $PI_TARGET_DIR"
echo "Branch: $REPO_BRANCH"
echo "Message page:"
  echo "  http://$(hostname).local:3000/messages"
echo "If you ever re-run setup.sh, your previous config.json backups are in:"
echo "  ~/${APP_NAME}-backups/"
echo ""

if [[ "$SESSION_TYPE" == "wayland" ]]; then
  echo "NOTE (Trixie/labwc): auto-start is handled by labwc autostart."
fi

echo ""
echo "IMPORTANT:"
echo "A reboot is required to start the display automatically."
echo "Run: sudo reboot"
echo ""
