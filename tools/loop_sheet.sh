#!/usr/bin/env bash
# One "beautiful Mars" iteration sheet: photo | render (default face) | render (yaw 160),
# 800 px raw (colours from the world's photo profile, applied by earthgen), plus the
# same two faces as 128 px pixel art next to the reference (OUT_128.png).
#   tools/loop_sheet.sh renders/loop/iter01.png [extra earthgen args]
set -euo pipefail
out=$1; shift
root=$(cd "$(dirname "$0")/.." && pwd)
tmp=$(mktemp -d)
for y in 0 160; do
    "$root/build/earthgen" --planet mars --size 800 --raw --sun-az 43 --grid 512 --yaw "$y" "$@" \
        --out "$tmp/c$y.png" 2>/dev/null
    "$root/build/earthgen" --planet mars --sun-az 43 --yaw "$y" "$@" --out "$tmp/p$y.png" 2>/dev/null
done
mkdir -p "$(dirname "$out")"
uv run -q --no-project --with pillow python - "$root/solar_system_reference/mars.jpg" "$tmp/c0.png" "$tmp/c160.png" "$out" <<'EOF'
import sys
from PIL import Image
ims = [Image.open(p).convert("RGB").resize((800, 800), Image.LANCZOS) for p in sys.argv[1:4]]
s = Image.new("RGB", (2400, 800))
for i, im in enumerate(ims):
    s.paste(im, (800 * i, 0))
s.save(sys.argv[4])
EOF
uv run -q --no-project --with pillow python - "$root/references/solar/mars_x4.png" "$tmp/p0_x4.png" "$tmp/p160_x4.png" "${out%.png}_128.png" <<'EOF'
import sys
from PIL import Image
ims = [Image.open(p).convert("RGB").resize((512, 512), Image.NEAREST) for p in sys.argv[1:4]]
s = Image.new("RGB", (1536, 512))
for i, im in enumerate(ims):
    s.paste(im, (512 * i, 0))
s.save(sys.argv[4])
EOF
rm -rf "$tmp"
echo "wrote $out"
