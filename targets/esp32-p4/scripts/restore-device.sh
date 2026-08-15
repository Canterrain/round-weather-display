#!/usr/bin/env bash
set -euo pipefail

source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/common.sh"
require_idf

PORT="${1:-}"
BACKUP_IMAGE="${2:-}"

if [[ -z "$PORT" || -z "$BACKUP_IMAGE" ]]; then
  echo "Usage: $0 <serial-port> <backup-image>"
  exit 1
fi

if [[ ! -f "$BACKUP_IMAGE" ]]; then
  echo "ERROR: backup image not found: $BACKUP_IMAGE"
  exit 1
fi

python -m esptool \
  --chip esp32p4 \
  -p "$PORT" \
  -b 460800 \
  --before default_reset \
  --after hard_reset \
  write_flash 0 "$BACKUP_IMAGE"
