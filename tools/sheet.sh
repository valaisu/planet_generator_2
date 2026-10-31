#!/usr/bin/env bash
# Contact sheet: N generated planets (seeds 1..N) next to the 4 references.
#   tools/sheet.sh OUT.png [N=8] [extra earthgen args...]
set -euo pipefail
cd "$(dirname "$0")/.."
out=${1:-sheet.png}; n=${2:-8}; shift $(( $# >= 2 ? 2 : $# )) || true
tmp=$(mktemp -d)
inputs=()
for s in $(seq 1 "$n"); do
    ./build/earthgen --seed "$s" --out "$tmp/s$s.png" --scale 2 "$@" 2>/dev/null
    inputs+=(-i "$tmp/s${s}_x2.png")
done
for r in earth_0 earth_1 earth_2 earth_3; do
    ffmpeg -loglevel error -y -i "references/${r}_x4.png" -vf scale=256:256:flags=neighbor "$tmp/$r.png"
    inputs+=(-i "$tmp/$r.png")
done
total=$(( n + 4 )); cols=4
layout=""
for i in $(seq 0 $(( total - 1 ))); do
    layout+="$(( (i % cols) * 256 ))_$(( (i / cols) * 256 ))|"
done
ffmpeg -loglevel error -y "${inputs[@]}" -filter_complex "xstack=inputs=$total:layout=${layout%|}:fill=black" "$out"
rm -rf "$tmp"
echo "wrote $out (seeds 1..$n, then references)"
