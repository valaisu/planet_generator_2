#!/usr/bin/env bash
# Disk + LEO of one seed at several --vibrance levels, one labelled row each.
#   tools/vibrance_sheet.sh OUT.png [SEED] [LEVELS...]
#   tools/vibrance_sheet.sh renders/vibrance_sheet.png 3 1.0 1.3 1.6 1.9 2.2 2.5
set -euo pipefail
cd "$(dirname "$0")/.."
out=$1; seed=${2:-3}; shift $(( $# < 2 ? $# : 2 ))
levels=${*:-1.0 1.3 1.6 1.9 2.2 2.5}
font=/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf
tmp=$(mktemp -d); rows=()
for v in $levels; do
    ./build/earthgen --seed "$seed" --vibrance "$v" --out "$tmp/d$v.png" 2>/dev/null
    ./build/earthgen --seed "$seed" --vibrance "$v" --view leo --out "$tmp/l$v.png" 2>/dev/null
    ffmpeg -loglevel error -y -i "$tmp/d${v}_x4.png" -i "$tmp/l${v}_x4.png" -filter_complex \
        "hstack=2,drawtext=fontfile=$font:text='vibrance $v':x=12:y=12:fontsize=36:fontcolor=white:box=1:boxcolor=black@0.6" \
        "$tmp/row$v.png"
    rows+=(-i "$tmp/row$v.png")
done
ffmpeg -loglevel error -y "${rows[@]}" -filter_complex "vstack=$(( ${#rows[@]} / 2 ))" "$out"
rm -rf "$tmp"
echo "$out"
