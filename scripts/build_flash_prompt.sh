#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-${ROOT_DIR}/.idfbuild}"
CACHE_DIR="${XDG_CACHE_HOME:-${ROOT_DIR}/.cache}"

mkdir -p "${BUILD_DIR}" "${CACHE_DIR}/Espressif/ComponentManager"

# Build + flash wrapper that prompts for commit/sub-version action when successful.
XDG_CACHE_HOME="${CACHE_DIR}" idf.py -B "${BUILD_DIR}" -DIDF_TARGET=esp32s3 reconfigure build flash

echo
echo "Build and flash completed successfully."
read -r -p "Commit current changes and create sub-version (bump Z)? [y/N]: " reply

if [[ "$reply" =~ ^[Yy]$ ]]; then
  ./scripts/bump_subversion.sh
  git add -A
  version="$(cat VERSION)"
  git commit -m "Release v${version}"
  echo "Committed release v${version}."
else
  echo "No commit/version bump performed."
fi
