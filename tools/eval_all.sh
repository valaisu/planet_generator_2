#!/usr/bin/env bash
# Evaluate every preset against all of its reference photos (tools/evaluate.py):
# raw render vs raw references (surface model only) and paletted vs paletted
# (the final look). Sun turned toward the camera for bodies photographed near
# full phase. Writes renders/eval/<body>_{raw,pal}.png (+ _spectrum.png) and
# renders/eval/summary.txt.
#   tools/eval_all.sh [extra earthgen args...]
set -euo pipefail
cd "$(dirname "$0")/.."
out=renders/eval; mkdir -p "$out"; : > "$out/summary.txt"
EV="uv run -q --no-project --with numpy --with pillow --with scipy --with matplotlib python tools/evaluate.py"
declare -A AZ=([mercury]=0 [venus]=0 [mars]=43 [europa]=30 [titan]=30 [pluto]=30 [earth]=0)
declare -A REFS=(
    [mercury]="solar/mercury"
    [venus]="solar/venus extra/venus_pia23791 extra/venus_realcolor"
    [mars]="solar/mars extra/mars_osiris extra/mars_hubble1999"
    [europa]="solar/europa extra/europa_juno extra/europa_natural"
    [titan]="solar/titan extra/titan_natural extra/titan_cassini"
    [pluto]="solar/pluto extra/pluto_accurate extra/pluto_nh2x"
    [earth]="earth_0 earth_1 earth_2 earth_3")
for b in mercury venus mars europa titan pluto earth; do
    ./build/earthgen --planet "$b" --sun-az "${AZ[$b]}" --raw --out "$out/${b}_render_raw.png" "$@" 2>/dev/null
    ./build/earthgen --planet "$b" --sun-az "${AZ[$b]}" --out "$out/${b}_render_pal.png" "$@" 2>/dev/null
    raw=(); pal=()
    for r in ${REFS[$b]}; do raw+=("references/raw/$(basename "$r").png"); pal+=("references/$r.png"); done
    { echo "=== $b (raw)"; $EV "$out/${b}_raw.png" --ref "${raw[@]}" --render "$out/${b}_render_raw.png";
      echo "=== $b (paletted)"; $EV "$out/${b}_pal.png" --ref "${pal[@]}" --render "$out/${b}_render_pal.png"; } >> "$out/summary.txt"
done
grep -E "^===|^vs refs" "$out/summary.txt"
