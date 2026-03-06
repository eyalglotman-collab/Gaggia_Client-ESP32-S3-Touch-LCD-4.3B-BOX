#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

mkdir -p "${ROOT_DIR}/.cache/Espressif/ComponentManager" "${ROOT_DIR}/.idfbuild"

export XDG_CACHE_HOME="${ROOT_DIR}/.cache"

if ! command -v idf.py >/dev/null 2>&1; then
  echo "idf.py is not available in PATH."
  echo "Initialize your local ESP-IDF environment first (for example: export.sh or ESP-IDF PowerShell)."
  return 1 2>/dev/null || exit 1
fi

echo "IDF_PATH=${IDF_PATH:-<set by your local ESP-IDF environment>}"
echo "IDF_TOOLS_PATH=${IDF_TOOLS_PATH:-<set by your local ESP-IDF environment>}"
echo "XDG_CACHE_HOME=${XDG_CACHE_HOME}"
echo "Build dir: ${ROOT_DIR}/.idfbuild"
echo ""
echo "Run commands with:"
echo "  XDG_CACHE_HOME=${XDG_CACHE_HOME} idf.py -B ${ROOT_DIR}/.idfbuild -DIDF_TARGET=esp32s3 reconfigure"
echo "  XDG_CACHE_HOME=${XDG_CACHE_HOME} idf.py -B ${ROOT_DIR}/.idfbuild build"
echo "  XDG_CACHE_HOME=${XDG_CACHE_HOME} idf.py -B ${ROOT_DIR}/.idfbuild -p <PORT> flash monitor"
