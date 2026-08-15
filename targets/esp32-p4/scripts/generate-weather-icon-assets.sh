#!/bin/bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
ICON_SOURCE_DIR="${ROOT_DIR}/shared/assets/icons"
OUTPUT_DIR="${ROOT_DIR}/targets/esp32-p4/main/assets"
TMP_DIR="${TMPDIR:-/private/tmp}/rwd-weather-icon-assets"
PYTHON_BIN="${ROOT_DIR}/.venv/bin/python"
ICON_SIZE=250

mkdir -p "${TMP_DIR}" "${OUTPUT_DIR}"

ICON_NAMES=()
while IFS= read -r icon_name; do
  ICON_NAMES+=("${icon_name}")
done < <(
  find "${ICON_SOURCE_DIR}" -maxdepth 1 -type f -name '*.svg' -exec basename {} .svg \; | sort
)

if [[ "${#ICON_NAMES[@]}" -eq 0 ]]; then
  echo "No weather SVG assets found in ${ICON_SOURCE_DIR}" >&2
  exit 1
fi

for icon_name in "${ICON_NAMES[@]}"; do
  svg_path="${ICON_SOURCE_DIR}/${icon_name}.svg"
  png_path="${TMP_DIR}/${icon_name}.svg.png"
  symbol_name="weather_icon_${icon_name//-/_}"

  "${PYTHON_BIN}" "${ROOT_DIR}/targets/esp32-p4/tools/rasterize_svg.py" \
    "${svg_path}" \
    "${png_path}" \
    "${ICON_SIZE}"

  "${PYTHON_BIN}" "${ROOT_DIR}/targets/esp32-p4/tools/generate_lvgl_image.py" \
    --input "${png_path}" \
    --symbol "${symbol_name}" \
    --format argb8888 \
    --output-c "${OUTPUT_DIR}/${symbol_name}.c" \
    --output-h "${OUTPUT_DIR}/${symbol_name}.h"
done

"${SCRIPT_DIR}/update-asset-source-manifest.sh"
