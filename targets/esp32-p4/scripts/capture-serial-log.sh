#!/usr/bin/env bash
set -euo pipefail

source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/common.sh"
require_idf

PORT="${1:-}"
OUTPUT_PATH="${2:-}"
DURATION_SECONDS="${3:-150}"

if [[ -z "$PORT" || -z "$OUTPUT_PATH" ]]; then
  echo "Usage: $0 <serial-port> <output-path> [duration-seconds]"
  exit 1
fi

mkdir -p "$(dirname "$OUTPUT_PATH")"

python "$PROJECT_DIR/tools/capture_serial.py" \
  --port "$PORT" \
  --baud 115200 \
  --output "$OUTPUT_PATH" \
  --duration "$DURATION_SECONDS" \
  --reset-before-capture \
  --until "BENCH_RUN_COMPLETE" \
  --require-until \
  --settle-seconds 2
