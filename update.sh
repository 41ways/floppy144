#!/bin/sh
# Pull the newest build without a GitHub login. The rolling "latest" release is
# overwritten by CI on every push to main, so this always fetches the tip.
set -e
OUT="${1:-$HOME/play}"
mkdir -p "$OUT"
curl -fL -o "$OUT/SOUNDING.exe" \
  https://github.com/41ways/floppy144/releases/download/latest/SOUNDING.exe
echo "updated $OUT/SOUNDING.exe -- $(stat -c%s "$OUT/SOUNDING.exe") bytes"
