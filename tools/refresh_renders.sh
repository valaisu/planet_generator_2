#!/usr/bin/env bash
# Wipe renders/ and regenerate the standard set, so everything in it is current.
#   tools/refresh_renders.sh
set -euo pipefail
cd "$(dirname "$0")/.."
rm -rf renders
mkdir -p renders
E=data/earth_relief_1deg.f32
tools/sheet.sh renders/sheet.png 8 >/dev/null                       # seeds 1-8 + references
tools/solar_sheet.sh renders/solar_sheet.png >/dev/null              # presets from physics vs photos
uv run -q --no-project --with pillow python tools/pair_sheet.py renders/pair_sheet.png >/dev/null  # each reference | generated
mkdir -p renders/planets
for b in mercury venus earth mars europa titan pluto; do
    ./build/earthgen --planet "$b" --out "renders/planets/$b.png" 2>/dev/null
done
tmp=$(mktemp -d)
for s in 2 3 5; do ./build/earthgen --seed "$s" --view leo --out "$tmp/l$s.png" 2>/dev/null; done
ffmpeg -loglevel error -y -i "$tmp/l2_x4.png" -i "$tmp/l3_x4.png" -i "$tmp/l5_x4.png" \
    -i references/leo_0_x4.png -i references/leo_1_x4.png -filter_complex vstack=5 renders/leo_sheet.png
rm -rf "$tmp"
# Night side (city lights): disks of seeds 2, 3, 5 + real Earth relief, then LEO after dusk.
tmp=$(mktemp -d)
for s in 2 3 5; do ./build/earthgen --seed "$s" --sun-az 150 --out "$tmp/n$s.png" 2>/dev/null; done
./build/earthgen --earth "$E" --yaw 80 --sun-az 150 --out "$tmp/ne.png" 2>/dev/null
ffmpeg -loglevel error -y -i "$tmp/n2_x4.png" -i "$tmp/n3_x4.png" -i "$tmp/n5_x4.png" -i "$tmp/ne_x4.png" \
    -filter_complex hstack=4 renders/night_sheet.png
./build/earthgen --seed 3 --view leo --sun-elev -15 --out "$tmp/ln3.png" 2>/dev/null
./build/earthgen --seed 5 --view leo --sun-elev -15 --out "$tmp/ln5.png" 2>/dev/null
ffmpeg -loglevel error -y -i "$tmp/ln3_x4.png" -i "$tmp/ln5_x4.png" -filter_complex vstack=2 renders/leo_night_sheet.png
rm -rf "$tmp"
tools/vibrance_sheet.sh renders/vibrance_sheet.png 3 1.0 1.3 1.6 1.9 2.2 2.5 >/dev/null
tools/animate.sh renders/orbit_seed3 720 24 --seed 3 --orbits 1 --cloud-minutes 20 >/dev/null
tools/animate.sh renders/clouds_seed3 360 24 --seed 3 --orbits 0 --cloud-minutes 20 >/dev/null
tools/animate.sh renders/leo_fly_seed3 720 24 --seed 3 --view leo --km-per-frame 1 >/dev/null
for a in orbit_seed3 clouds_seed3 leo_fly_seed3; do rm -rf "renders/${a}_frames" "renders/$a.png"; done
./build/earthgen --seed 2 --maps renders/seed2_maps --out renders/seed2.png 2>/dev/null
./build/earthgen --earth "$E" --yaw -135 --maps renders/earth_maps --out renders/earth_australia.png 2>/dev/null
./build/earthgen --earth "$E" --view leo --lat 25 --lon -100 --heading 90 --out renders/leo_earth_mexico.png 2>/dev/null
cat > renders/README.txt <<TXT
Generated $(date '+%Y-%m-%d %H:%M') by tools/refresh_renders.sh — everything here is from that run.
sheet.png              seeds 1-8 (disk view, 18-colour palette) above the 4 references
solar_sheet.png        Mercury..Pluto rendered from their physics (--planet), under the stylized photos
pair_sheet.png         every pixelized disk reference (left) next to its generated preset (right), labelled
planets/<name>.png     each preset from its physics (+ _x4 upscale)
leo_sheet.png          low Earth orbit: seeds 2, 3, 5, then references leo_0 (Florida), leo_1 (Sahara coast)
seed2*.png, seed2_maps/            generated planet 2 + equirect maps of every field
earth_australia*.png, earth_maps/  simulation run on real Earth relief (disk, Australia)
leo_earth_mexico*.png              real Earth relief from low orbit, looking east over the Gulf
orbit_seed3.gif/.mp4   one slow orbit of seed 3 (30 s), sun fixed, clouds drifting (20 min/frame)
clouds_seed3.gif/.mp4  cloud demo: fixed camera, 120 h of weather at 20 min/frame
leo_fly_seed3.gif/.mp4 fly-along at 400 km: 720 km of ground in 30 s (1 km/frame), clouds frozen
vibrance_sheet.png     seed 3 disk + LEO at --vibrance 1.0 .. 2.5 (default 1.3)
night_sheet.png        night side with city lights: seeds 2, 3, 5 + real Earth relief (Americas)
leo_night_sheet.png    low orbit after dusk (sun 15 deg below the horizon): seeds 3, 5
*_maps/rivers_cities.png           rivers + cities over habitability
TXT
echo "renders/ refreshed"
