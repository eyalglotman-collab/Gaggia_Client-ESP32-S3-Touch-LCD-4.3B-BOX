#!/usr/bin/env bash
set -euo pipefail

if [[ ! -f VERSION ]]; then
  echo "VERSION file not found" >&2
  exit 1
fi

current="$(tr -d '[:space:]' < VERSION)"
if [[ ! "$current" =~ ^([0-9]+)\.([0-9]+)\.([0-9]+)$ ]]; then
  echo "Invalid VERSION format: $current" >&2
  exit 1
fi

x="${BASH_REMATCH[1]}"
y="${BASH_REMATCH[2]}"
z="${BASH_REMATCH[3]}"
new_z=$((z + 1))
new_version="${x}.${y}.${new_z}"

printf '%s\n' "$new_version" > VERSION

echo "Version bumped: $current -> $new_version"
