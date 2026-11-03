"""Fit the planet disk of a photo whose limb is partly dark (terminator, night side).

Takes the boundary of the largest bright blob, fits a circle, then repeatedly drops
boundary points inside it (terminator points lie inside the true limb) and refits.
Prints cx,cy,r for `stylize_ref --circle`.
    uv run --no-project --with numpy --with pillow --with scipy python tools/fit_disk.py photo.jpg [thresh]
"""
import sys, numpy as np
from PIL import Image
from scipy import ndimage
f, th = sys.argv[1], float(sys.argv[2]) if len(sys.argv) > 2 else 0.03
a = np.asarray(Image.open(f).convert('RGB')).astype(np.float32).max(2) / 255
m = a > th
lab, n = ndimage.label(m)
big = lab == (np.argmax(np.bincount(lab.ravel())[1:]) + 1)
big = ndimage.binary_fill_holes(big)
edge = big & ~ndimage.binary_erosion(big)
y, x = np.nonzero(edge); P = np.c_[x, y].astype(float) + 0.5
def fit(P):
    A = np.c_[2 * P, np.ones(len(P))]; b = (P ** 2).sum(1)
    cx, cy, c = np.linalg.lstsq(A, b, rcond=None)[0]; return cx, cy, np.sqrt(c + cx * cx + cy * cy)
cx, cy, r = fit(P)
for _ in range(30):
    d = np.hypot(P[:, 0] - cx, P[:, 1] - cy)
    keep = d > r - 0.01 * r
    cx, cy, r = fit(P[keep])
print(f"{cx:.1f},{cy:.1f},{r:.1f}  kept {keep.mean():.2f} of edge")
