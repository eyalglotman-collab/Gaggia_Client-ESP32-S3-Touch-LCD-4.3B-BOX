#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-${ROOT_DIR}/.idfbuild}"
CACHE_DIR="${XDG_CACHE_HOME:-${ROOT_DIR}/.cache}"

mkdir -p "${BUILD_DIR}" "${CACHE_DIR}/Espressif/ComponentManager"

# Best-effort COM release through simulator API before/after build+flash.
invoke_simulator_com_release() {
  if command -v curl >/dev/null 2>&1; then
    curl -sS -m 3 -X POST "http://localhost:8000/api/transport/release-com" >/dev/null 2>&1 || true
  elif command -v powershell.exe >/dev/null 2>&1; then
    powershell.exe -NoProfile -ExecutionPolicy Bypass -Command \
      "try { Invoke-WebRequest -Uri 'http://localhost:8000/api/transport/release-com' -Method Post -TimeoutSec 3 -UseBasicParsing -ErrorAction SilentlyContinue | Out-Null } catch {}" \
      >/dev/null 2>&1 || true
  fi
  sleep 0.5
}

release_after_build_flash() {
  echo "Releasing COM port after build/flash..."
  invoke_simulator_com_release
}

trap release_after_build_flash EXIT

echo "Releasing COM port before build/flash..."
invoke_simulator_com_release

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
