#!/usr/bin/env bash
set -euo pipefail

source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/common.sh"
require_idf

PORT="${1:-}"
if [[ -z "$PORT" ]]; then
  echo "Usage: $0 <serial-port>"
  exit 1
fi

parity_lab_idf -p "$PORT" flash
