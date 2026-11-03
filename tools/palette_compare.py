"""Colours actually present in pixelized images (reference vs render): each image's
distinct colours on the planet disk, sorted by luma, with their share of the disk,
as text and as a swatch sheet (bar width = share).
    uv run --no-project --with numpy --with pillow python tools/palette_compare.py OUT.png ref.png render.png [...]
"""
import sys
import numpy as np
from PIL import Image, ImageDraw, ImageFont


def disk_colours(f):
    a = np.asarray(Image.open(f).convert("RGB"))
    n = a.shape[0]
    y, x = np.mgrid[:n, :a.shape[1]]
    disk = np.hypot(x - a.shape[1] / 2 + 0.5, y - n / 2 + 0.5) < 0.84 * n / 2 - 1  # earthgen framing
    px = a[disk].reshape(-1, 3)
    cols, counts = np.unique(px, axis=0, return_counts=True)
    order = np.argsort(cols @ [0.299, 0.587, 0.114])
    return cols[order], counts[order] / counts.sum()


out, files = sys.argv[1], sys.argv[2:]
W, ROW = 900, 46
sheet = Image.new("RGB", (W, ROW * len(files) + 10), (12, 12, 16))
draw = ImageDraw.Draw(sheet)
try:
    font = ImageFont.load_default(size=13)
except TypeError:
    font = ImageFont.load_default()
for i, f in enumerate(files):
    cols, share = disk_colours(f)
    print(f"{f}: {len(cols)} colours on the disk")
    for c, s in zip(cols, share):
        print(f"   #{c[0]:02x}{c[1]:02x}{c[2]:02x}  {100 * s:5.1f}%")
    y, x = 10 + i * ROW, 200
    draw.text((6, y + 10), f.split("/")[-1], fill=(230, 230, 235), font=font)
    for c, s in zip(cols, share):
        w = max(2, int(round(s * (W - 210))))
        draw.rectangle([x, y, x + w - 1, y + ROW - 12], fill=tuple(int(v) for v in c))
        x += w
sheet.save(out)
