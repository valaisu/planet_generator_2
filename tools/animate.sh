#!/usr/bin/env bash
# Render an animation: disk orbit (camera circles the planet, sun fixed: day -> night
# with city lights -> day) or, with --view leo, a fly-along; assemble GIF + MP4.
#   tools/animate.sh OUT_STEM [FRAMES] [FPS] [earthgen args...]
#   tools/animate.sh renders/orbit_seed3 240 24 --seed 3 --orbits 2
set -euo pipefail
cd "$(dirname "$0")/.."
stem=$1; frames=${2:-240}; fps=${3:-24}
shift $(( $# < 3 ? $# : 3 ))
rm -rf "${stem}_frames"
./build/earthgen --out "$stem.png" --frames "$frames" "$@" 2>/dev/null
# Frames hold <= 19 colours, so the GIF palette is exact (no re-dithering).
# Two passes: a split filter would buffer every frame in memory.
ffmpeg -loglevel error -y -i "${stem}_frames/f%04d_x4.png" \
    -vf palettegen=reserve_transparent=0:stats_mode=full "${stem}_frames/palette.png"
ffmpeg -loglevel error -y -framerate "$fps" -i "${stem}_frames/f%04d_x4.png" -i "${stem}_frames/palette.png" \
    -lavfi "paletteuse=dither=none" "$stem.gif"
ffmpeg -loglevel error -y -framerate "$fps" -i "${stem}_frames/f%04d_x4.png" \
    -c:v libx264 -pix_fmt yuv420p -crf 12 "$stem.mp4"
echo "$stem.gif  $stem.mp4  (frames in ${stem}_frames/)"
