#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 1 ]]; then
  echo "Usage: $0 <source_demo_esp-idf_path>" >&2
  exit 1
fi

SRC="$1"
DST="external_examples/waveshare_esp-idf"

if [[ ! -d "$SRC" ]]; then
  echo "Source path does not exist: $SRC" >&2
  exit 1
fi

mkdir -p "$DST"
cp -a "$SRC"/. "$DST"/

echo "Imported Waveshare ESP-IDF examples to: $DST"
echo "Example folders:"
find "$DST" -maxdepth 2 -type d | sed '1d' | head -n 60
