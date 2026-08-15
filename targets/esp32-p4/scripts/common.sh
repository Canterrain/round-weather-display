#!/usr/bin/env bash
set -euo pipefail

PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BENCHMARK_BUILD_DIR="$PROJECT_DIR/build-benchmark"
BENCHMARK_SDKCONFIG="$PROJECT_DIR/sdkconfig.benchmark.generated"
BENCHMARK_SDKCONFIG_DEFAULTS="sdkconfig;sdkconfig.defaults;sdkconfig.benchmark"
BENCHMARK_TUNED_BUILD_DIR="$PROJECT_DIR/build-benchmark-tuned"
BENCHMARK_TUNED_SDKCONFIG="$PROJECT_DIR/sdkconfig.benchmark.tuned.generated"
BENCHMARK_TUNED_SDKCONFIG_DEFAULTS="sdkconfig;sdkconfig.defaults;sdkconfig.benchmark;sdkconfig.benchmark.tuned"
PARITY_LAB_BUILD_DIR="$PROJECT_DIR/build-parity-lab"
PARITY_LAB_SDKCONFIG="$PROJECT_DIR/sdkconfig.paritylab.generated"
PARITY_LAB_SDKCONFIG_DEFAULTS="sdkconfig;sdkconfig.defaults;sdkconfig.paritylab"
FLASH_BACKUP_SIZE_BYTES="0x2000000"

require_idf() {
  if ! command -v idf.py >/dev/null 2>&1; then
    echo "ERROR: idf.py was not found in PATH."
    echo "Activate your ESP-IDF environment first, then retry."
    exit 1
  fi
}

benchmark_idf() {
  idf.py \
    -C "$PROJECT_DIR" \
    -B "$BENCHMARK_BUILD_DIR" \
    -D "SDKCONFIG=$BENCHMARK_SDKCONFIG" \
    -D "SDKCONFIG_DEFAULTS=$BENCHMARK_SDKCONFIG_DEFAULTS" \
    "$@"
}

benchmark_tuned_idf() {
  idf.py \
    -C "$PROJECT_DIR" \
    -B "$BENCHMARK_TUNED_BUILD_DIR" \
    -D "SDKCONFIG=$BENCHMARK_TUNED_SDKCONFIG" \
    -D "SDKCONFIG_DEFAULTS=$BENCHMARK_TUNED_SDKCONFIG_DEFAULTS" \
    "$@"
}

parity_lab_idf() {
  idf.py \
    -C "$PROJECT_DIR" \
    -B "$PARITY_LAB_BUILD_DIR" \
    -D "SDKCONFIG=$PARITY_LAB_SDKCONFIG" \
    -D "SDKCONFIG_DEFAULTS=$PARITY_LAB_SDKCONFIG_DEFAULTS" \
    "$@"
}
