#!/usr/bin/env bash
# Rebuild every pixelized reference from the photos: references/... (palette +
# dither, like the renders) and references/raw/... (no palette, for statistics
# against `earthgen --raw`). Disk circles for photos with dark limbs come from
# tools/fit_disk.py (threshold noted).
#   tools/make_references.sh
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p references/solar references/extra references/raw
both() {  # SRC OUT_NAME OUT_DIR [stylize_ref args]
    local src=$1 name=$2 dir=$3; shift 3
    ./build/stylize_ref "$src" "$dir/$name.png" "$@" >/dev/null
    ./build/stylize_ref "$src" "references/raw/$name.png" --raw "$@" >/dev/null
}
for f in Earth_0 earth_1 earth_2 earth_3; do both "earth_images/$f.jpg" "${f,,}" references; done
./build/stylize_ref earth_images/leo_0.jpeg references/leo_0.png --frame --crop 0,100,678,339 >/dev/null
./build/stylize_ref earth_images/leo_1.jpeg references/leo_1.png --frame --crop 210,200,860,430 >/dev/null
R=solar_system_reference
both $R/mars.jpg mars references/solar
both $R/europa.jpg europa references/solar
both $R/mercury.jpg mercury references/solar --circle 516.0,519.6,477.1
both $R/venus.jpg venus references/solar --circle 821.3,450.6,375.9
both $R/titan.jpg titan references/solar --circle 299.1,304.2,261.9
both $R/pluto.jpg pluto references/solar --circle 4063.6,4039.9,3565.0
both $R/pluto_enhanced_poster.jpg pluto_enhanced references/solar --circle 990.3,990.0,792.8      # thresh 0.15, cropped
both $R/titan_infrared_falsecolor.jpg titan_infrared references/solar --circle 396.5,282.0,206.0  # thresh 0.25
# Extra true-colour photos (Wikimedia Commons, NASA/ESA), thresh 0.06 unless noted.
E=$R/extra
both $E/europa_juno.png europa_juno references/extra --circle 847.7,845.6,778.8
both $E/europa_natural.png europa_natural references/extra --circle 963.3,956.4,833.9
both $E/mars_hubble1999.jpg mars_hubble1999 references/extra --circle 238.9,240.7,153.6   # thresh 0.2
both $E/mars_osiris.jpg mars_osiris references/extra --circle 954.9,959.9,814.6
both $E/pluto_accurate.png pluto_accurate references/extra --circle 973.6,965.3,849.1
both $E/pluto_nh2x.jpg pluto_nh2x references/extra --circle 898.7,885.6,838.9
both $E/titan_cassini.jpg titan_cassini references/extra --circle 259.3,297.5,282.2
both $E/titan_natural.jpg titan_natural references/extra --circle 252.7,255.0,185.8
both $E/venus_pia23791.jpg venus_pia23791 references/extra --circle 502.6,502.9,468.0
both $E/venus_realcolor.jpg venus_realcolor references/extra --circle 244.0,244.9,231.7
echo "references rebuilt"
