"""Small-scale detail of pixelized disks: on lit planet pixels (luma > 0.12, away from
the limb), the RMS of luma minus a 2 px Gaussian blur (1-3 px features), the
large-scale contrast (sd of the blurred luma) and the mean colour.
    uv run --no-project --with numpy --with pillow --with scipy python tools/detail_stats.py A.png [B.png ...]
"""
import sys
import numpy as np
from PIL import Image
from scipy import ndimage

for f in sys.argv[1:]:
    a = np.asarray(Image.open(f).convert("RGB")).astype(float) / 255
    luma = a @ [0.299, 0.587, 0.114]
    n = luma.shape[0]
    y, x = np.mgrid[:n, :n]
    disk = np.hypot(x - n / 2 + 0.5, y - n / 2 + 0.5) < 0.84 * n / 2 - 4  # earthgen framing, minus the limb
    blur = ndimage.gaussian_filter(luma, 2.0)
    m = disk & (blur > 0.12)
    hp = (luma - blur)[m]
    print(f"{f:45s} detail {np.sqrt((hp ** 2).mean()):.4f}  contrast {blur[m].std():.3f}  "
          f"mean rgb {' '.join(f'{v:.2f}' for v in a[m].mean(0))}  ({m.mean():.0%} lit)")
