#!/usr/bin/env bash
set -euo pipefail

# Regenerates the Conditions view's image assets. All are A8 (alpha only):
# the firmware tints them at runtime (blue normally, amber during an alert,
# red under night shift), so one small asset covers every state. The ESP32's
# bootloader can only map ~16MB of firmware and the build is already close,
# so these stay as small as possible -- see render_edge_indicator.py.

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
REPO_ROOT="$(cd "$PROJECT_DIR/../.." && pwd)"
PYTHON_BIN="$REPO_ROOT/.venv/bin/python"
SRC_DIR="$PROJECT_DIR/assets-src/conditions"

if [[ ! -x "$PYTHON_BIN" ]]; then
  echo "ERROR: expected CairoSVG virtualenv at $PYTHON_BIN"
  exit 1
fi

to_lvgl_a8() {
  local png_path="$1"
  local symbol="$2"
  "$PYTHON_BIN" "$PROJECT_DIR/tools/generate_lvgl_image.py" \
    --input "$png_path" \
    --symbol "$symbol" \
    --format a8 \
    --output-c "$PROJECT_DIR/main/assets/${symbol}.c" \
    --output-h "$PROJECT_DIR/main/assets/${symbol}.h"
}

render_svg_a8() {
  local svg_name="$1"
  local symbol="$2"
  shift 2
  local png_path="/private/tmp/${symbol}.png"
  "$PYTHON_BIN" "$PROJECT_DIR/tools/rasterize_svg.py" "$SRC_DIR/${svg_name}.svg" "$png_path" "$@"
  to_lvgl_a8 "$png_path" "$symbol"
}

# Amber storm-alert glow on the right edge (mirror of the red message glow).
weather_glow_png="/private/tmp/conditions_edge_indicator.png"
"$PYTHON_BIN" "$PROJECT_DIR/tools/render_edge_indicator.py" --variant weather --output "$weather_glow_png"
to_lvgl_a8 "$weather_glow_png" conditions_edge_indicator

# Wind arrow: 36x400 viewBox strip at the 334px compass scale.
render_svg_a8 wind_arrow conditions_wind_arrow --width 30 --height 334

render_svg_a8 icon_wind conditions_icon_wind 36
render_svg_a8 icon_pressure conditions_icon_pressure 36
render_svg_a8 icon_rain conditions_icon_rain 36
render_svg_a8 icon_snow conditions_icon_snow 36

"$SCRIPT_DIR/update-asset-source-manifest.sh"
echo "Conditions assets regenerated."
