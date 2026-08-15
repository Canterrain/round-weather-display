#!/usr/bin/env bash
set -euo pipefail

source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/common.sh"
require_idf

PORT="${1:-}"
OUT_DIR="${2:-$PROJECT_DIR/backups/$(date +%Y%m%d-%H%M%S)}"

if [[ -z "$PORT" ]]; then
  echo "Usage: $0 <serial-port> [output-dir]"
  exit 1
fi

mkdir -p "$OUT_DIR"

python -m esptool \
  --chip esp32p4 \
  -p "$PORT" \
  -b 460800 \
  read_flash 0 "$FLASH_BACKUP_SIZE_BYTES" "$OUT_DIR/flash-32mb.bin"

printf '%s\n' "$OUT_DIR/flash-32mb.bin"
