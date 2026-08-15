#!/usr/bin/env bash
set -euo pipefail

source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/common.sh"
require_idf

idf.py -C "$PROJECT_DIR" set-target esp32p4
