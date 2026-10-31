"""Compare the colours of a photo and a render, per material.

Pixels are labelled by colour alone (photos have no material labels), the same
way for both images: space = dark, cloud = bright + grey, ocean = blue-dominant,
land = the rest. Prints share, median RGB and saturation per class.

  uv run --no-project --with numpy --with pillow python tools/color_stats.py \
      earth_images/leo_0.jpeg:0,100,678,339 leo_raw_x4.png
"""
import sys

import numpy as np
from PIL import Image


def load(arg):
    path, _, crop = arg.partition(":")
    im = Image.open(path).convert("RGB")
    if crop:
        x, y, w, h = map(int, crop.split(","))
        im = im.crop((x, y, x + w, y + h))
    return np.asarray(im, dtype=np.float32) / 255.0


def classes(px):
    r, g, b = px[..., 0], px[..., 1], px[..., 2]
    mx, mn = px.max(-1), px.min(-1)
    sat = (mx - mn) / np.maximum(mx, 1e-3)
    space = mx < 0.08
    cloud = ~space & (mn > 0.55) & (sat < 0.25)
    ocean = ~space & ~cloud & (b > r + 0.03) & (b >= g)
    land = ~space & ~cloud & ~ocean
    return {"space": space, "cloud": cloud, "ocean": ocean, "land": land}, sat


def report(arg):
    px = load(arg)
    cls, sat = classes(px)
    n = px.shape[0] * px.shape[1]
    print(arg)
    for name, m in cls.items():
        if m.sum() < 20:
            print(f"  {name:6s} {100 * m.mean():5.1f}%")
            continue
        med = np.median(px[m], axis=0)
        q = np.percentile(px[m].mean(-1), [10, 90])
        print(f"  {name:6s} {100 * m.sum() / n:5.1f}%  median rgb {med[0]:.2f} {med[1]:.2f} {med[2]:.2f}"
              f"  sat {np.median(sat[m]):.2f}  luma p10-p90 {q[0]:.2f}-{q[1]:.2f}")


for a in sys.argv[1:]:
    report(a)
