#!/usr/bin/env bash
# Solar-system check: each preset rendered from its physics (bottom row) under
# its stylized reference photo (top row; earth = references/earth_2).
#   tools/solar_sheet.sh OUT.png [extra earthgen args...]
set -euo pipefail
cd "$(dirname "$0")/.."
out=${1:-solar_sheet.png}; shift || true
tmp=$(mktemp -d)
bodies=(mercury venus earth mars europa titan pluto)
top=(); bottom=()
for b in "${bodies[@]}"; do
    ./build/earthgen --planet "$b" --out "$tmp/$b.png" --scale 2 "$@" 2>/dev/null
    ref=references/solar/${b}_x4.png; [ "$b" = earth ] && ref=references/earth_2_x4.png
    ffmpeg -loglevel error -y -i "$ref" -vf scale=256:256:flags=neighbor "$tmp/ref_$b.png"
    top+=(-i "$tmp/ref_$b.png"); bottom+=(-i "$tmp/${b}_x2.png")
done
n=${#bodies[@]}
ffmpeg -loglevel error -y "${top[@]}" -filter_complex "hstack=$n" "$tmp/top.png"
ffmpeg -loglevel error -y "${bottom[@]}" -filter_complex "hstack=$n" "$tmp/bottom.png"
ffmpeg -loglevel error -y -i "$tmp/top.png" -i "$tmp/bottom.png" -filter_complex vstack=2 "$out"
rm -rf "$tmp"
echo "wrote $out (references above, physics renders below)"
