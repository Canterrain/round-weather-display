#!/usr/bin/env bash
set -euo pipefail

# -----------------------------------------------------------------------------
# Generates the custom LVGL fonts used for the clock time and temperature
# labels, at their true rendered pixel size, from the same Montserrat-Medium
# source LVGL's own built-in fonts use (managed_components/lvgl__lvgl/
# scripts/built_in_font/Montserrat-Medium.ttf) -- so they match the rest of
# the UI's typeface exactly.
#
# These labels used to render at lv_font_montserrat_48 (LVGL's largest
# built-in size) and get stretched 2-3x larger via a style transform_scale.
# Upscaling an already-rasterized bitmap glyph like that blurs it -- this is
# what produced the "low resolution / fuzzy" look reported on real hardware.
# Generating a font at the actual on-screen size (with anti-aliasing computed
# natively at that size) and dropping the transform_scale fixes it.
#
# Each font's glyph range is restricted to just the characters that label
# ever displays (digits, plus ':' for time or '-'/'°' for temperature),
# not the full ASCII range LVGL's own built-ins cover -- keeping each font
# small despite the much larger pixel size.
#
# Requires network access the first time (downloads lv_font_conv via npx).
# -----------------------------------------------------------------------------

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
OUTPUT_DIR="$PROJECT_DIR/main/assets"
FONT_TTF="$PROJECT_DIR/managed_components/lvgl__lvgl/scripts/built_in_font/Montserrat-Medium.ttf"
LV_FONT_CONV="lv_font_conv@1.5.3"

if [[ ! -f "$FONT_TTF" ]]; then
  echo "ERROR: Montserrat-Medium.ttf not found at $FONT_TTF"
  echo "Run 'idf.py build' at least once first so managed_components/ is populated."
  exit 1
fi

mkdir -p "$OUTPUT_DIR"

generate_font() {
  local symbol="$1"
  local size="$2"
  local range="$3"

  echo "Generating ${symbol} (${size}px)..."
  npx -y "$LV_FONT_CONV" \
    --bpp 4 \
    --size "$size" \
    --font "$FONT_TTF" \
    -r "$range" \
    --format lvgl \
    --no-compress \
    --no-prefilter \
    --force-fast-kern-format \
    -o "$OUTPUT_DIR/${symbol}.c"

  # lv_font_conv emits an LV_LVGL_H_INCLUDE_SIMPLE-gated include that falls
  # back to "lvgl/lvgl.h" -- a path this project's include layout doesn't
  # have (every other asset here just does #include "lvgl.h" directly, per
  # main/assets/weather_icon_clear_day.c). Collapse it to match.
  python3 - "$OUTPUT_DIR/${symbol}.c" <<'PYEOF'
import sys
path = sys.argv[1]
with open(path) as f:
    content = f.read()
old = '#ifdef LV_LVGL_H_INCLUDE_SIMPLE\n#include "lvgl.h"\n#else\n#include "lvgl/lvgl.h"\n#endif'
new = '#include "lvgl.h"'
if old not in content:
    sys.exit(f"ERROR: expected include guard not found in {path} -- lv_font_conv's output format may have changed")
with open(path, "w") as f:
    f.write(content.replace(old, new))
PYEOF

  cat > "$OUTPUT_DIR/${symbol}.h" <<EOF
#pragma once

#include "lvgl.h"

extern const lv_font_t ${symbol};
EOF
}

# Digits + colon -- the digital clock face's time label ("10:42").
generate_font "lv_font_clock_time_144" 144 "0x30-0x39,0x3A"

# Digits + minus + degree sign -- temperature can go below zero.
generate_font "lv_font_temp_105" 105 "0x2D,0x30-0x39,0xB0"
generate_font "lv_font_temp_102" 102 "0x2D,0x30-0x39,0xB0"

"$SCRIPT_DIR/update-asset-source-manifest.sh"

echo "Done. Regenerated fonts are in $OUTPUT_DIR -- rebuild the firmware to pick them up."
