"""Compare cloud-field statistics against a real cloud map (the measuring stick).

    uv run --no-project --with numpy --with pillow python tools/cloud_stats.py \
        data/cloud_combined_2048.jpg maps/cloud_alpha.png [more maps...]

Every input is an equirectangular greyscale opacity map (lon -180..180, lat 90
top), resampled to 720x360. The NASA map is converted with the same
brightness -> opacity mapping earthgen's --cloud-image uses.
Reports per map:
  opacity mix   share of the sphere that is clear (<0.1), thin (0.1-0.4),
                medium (0.4-0.7), opaque (>0.7) — area weighted
  mean by lat   mean opacity in 15-degree bands, north to south
  detail        share of variance at scales below ~300 km (field minus a blur),
                within +-60 deg — "how broken-up" the clouds are
  slope         log-log slope of the zonal power spectrum, 300-3000 km
                wavelengths (real clouds: about -5/3 -> -2)
"""
import sys

import numpy as np
from PIL import Image, ImageFilter

W, H = 720, 360


def load(path):
    im = Image.open(path).convert("L").resize((W, H), Image.BILINEAR)
    a = np.asarray(im, dtype=np.float64) / 255.0
    if "cloud_combined" in path:  # NASA brightness -> opacity, as in load_cloud_image
        a = np.clip((a - 0.08) / 0.8, 0.0, 1.0)
    return a


def stats(a):
    lat = (0.5 - (np.arange(H) + 0.5) / H) * 180.0
    w = np.cos(np.radians(lat))[:, None] * np.ones((1, W))
    w /= w.sum()
    bins = [(0.0, 0.1), (0.1, 0.4), (0.4, 0.7), (0.7, 1.01)]
    mix = [float((w * ((a >= lo) & (a < hi))).sum()) for lo, hi in bins]
    bands = [float(a[(lat < hi) & (lat >= hi - 15)].mean()) for hi in range(90, -90, -15)]
    mid = np.abs(lat) < 60
    im = Image.fromarray((a * 255).astype(np.uint8))
    blur = np.asarray(im.filter(ImageFilter.GaussianBlur(3)), dtype=np.float64) / 255.0
    detail = float(np.var((a - blur)[mid]) / max(np.var(a[mid]), 1e-9))
    rows = a[mid] - a[mid].mean(axis=1, keepdims=True)
    spec = (np.abs(np.fft.rfft(rows, axis=1)) ** 2).mean(axis=0)
    k = np.arange(spec.size)
    wl_km = 40000.0 / np.maximum(k, 1) * 0.7  # ~cos(45 deg) mean scale
    sel = (wl_km > 300) & (wl_km < 3000) & (k > 0)
    slope = float(np.polyfit(np.log(k[sel]), np.log(spec[sel]), 1)[0])
    return mix, bands, detail, slope


print(f"{'map':40s} {'clear thin  med  opaque':24s} {'detail':>6s} {'slope':>6s}  mean opacity by 15-deg band (N->S)")
for p in sys.argv[1:]:
    mix, bands, detail, slope = stats(load(p))
    name = p if len(p) <= 40 else "..." + p[-37:]
    print(f"{name:40s} " + " ".join(f"{m:.2f}" for m in mix) + f"   {detail:6.3f} {slope:6.2f}  "
          + " ".join(f"{b:.2f}" for b in bands))
