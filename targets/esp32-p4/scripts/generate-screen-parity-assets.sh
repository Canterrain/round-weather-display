#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
REPO_ROOT="$(cd "$PROJECT_DIR/../.." && pwd)"
PYTHON_BIN="$REPO_ROOT/.venv/bin/python"

if [[ ! -x "$PYTHON_BIN" ]]; then
  echo "ERROR: expected CairoSVG virtualenv at $PYTHON_BIN"
  exit 1
fi

render_stage_asset() {
  local variant="$1"
  local mode="$2"
  local symbol="$3"
  local svg_path="$PROJECT_DIR/assets-src/${symbol}.svg"
  local png_path="/private/tmp/${symbol}.png"

  "$PYTHON_BIN" "$PROJECT_DIR/tools/render_screen_stage_svg.py" \
    --variant "$variant" \
    --mode "$mode" \
    --output "$svg_path"
  "$PYTHON_BIN" "$PROJECT_DIR/tools/rasterize_svg.py" "$svg_path" "$png_path"
  "$PYTHON_BIN" "$PROJECT_DIR/tools/generate_lvgl_image.py" \
    --input "$png_path" \
    --symbol "$symbol" \
    --format rgb565 \
    --dither \
    --output-c "$PROJECT_DIR/main/assets/${symbol}.c" \
    --output-h "$PROJECT_DIR/main/assets/${symbol}.h"
}

render_alpha_asset() {
  local variant="$1"
  local mode="$2"
  local symbol="$3"
  local svg_path="$PROJECT_DIR/assets-src/${symbol}.svg"
  local png_path="/private/tmp/${symbol}.png"

  "$PYTHON_BIN" "$PROJECT_DIR/tools/render_screen_stage_svg.py" \
    --variant "$variant" \
    --mode "$mode" \
    --output "$svg_path"
  "$PYTHON_BIN" "$PROJECT_DIR/tools/rasterize_svg.py" "$svg_path" "$png_path"
  "$PYTHON_BIN" "$PROJECT_DIR/tools/generate_lvgl_image.py" \
    --input "$png_path" \
    --symbol "$symbol" \
    --format argb8888 \
    --output-c "$PROJECT_DIR/main/assets/${symbol}.c" \
    --output-h "$PROJECT_DIR/main/assets/${symbol}.h"
}

render_stage_asset digital_stage day digital_stage_day
render_stage_asset digital_stage night digital_stage_night
render_stage_asset round_stage day round_stage_day
render_stage_asset round_stage night round_stage_night
render_alpha_asset forecast_frame day forecast_tomorrow_frame_day
render_alpha_asset forecast_frame night forecast_tomorrow_frame_night
"$SCRIPT_DIR/update-asset-source-manifest.sh"

echo "Screen parity assets regenerated."
