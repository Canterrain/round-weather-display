#!/usr/bin/env bash
set -euo pipefail

if [[ -n "${BASH_SOURCE[0]:-}" ]]; then
  SCRIPT_SOURCE="${BASH_SOURCE[0]}"
elif [[ -n "${ZSH_VERSION:-}" ]]; then
  SCRIPT_SOURCE="$(eval 'print -r -- ${(%):-%x}')"
else
  SCRIPT_SOURCE="$0"
fi

SCRIPT_DIR="$(cd "$(dirname "$SCRIPT_SOURCE")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../../.." && pwd)"
IDF_ROOT="$REPO_ROOT/.esp-idf/esp-idf-v5.5.5"
IDF_TOOLS_PATH="$REPO_ROOT/.esp-idf/tools"
IDF_PYTHON_ENV_GLOB="$IDF_TOOLS_PATH/python_env/idf5.5_py3."*_env
if [[ ! -d "$IDF_ROOT" ]]; then
  echo "ERROR: ESP-IDF was not found at:"
  echo "  $IDF_ROOT"
  echo "Install it first, then re-run this script."
  exit 1
fi

export IDF_TOOLS_PATH
for env_dir in $IDF_PYTHON_ENV_GLOB; do
  if [[ -d "$env_dir" ]]; then
    export IDF_PYTHON_ENV_PATH="$env_dir"
    break
  fi
done

. "$IDF_ROOT/export.sh"
