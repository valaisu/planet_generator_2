"""Material colours of a disk image: central pixels (r < 0.55 R: little limb/phase
shading at near-full phase), principal colour line in CIELAB -> sRGB colours at its
dark end / middle / bright end (5th/50th/95th percentile), and 3 k-means clusters
with their area share. Use on raw references (stylize_ref --raw) and raw renders.
    uv run --no-project --with numpy --with pillow --with scipy python tools/endmembers.py IMG.png [...]
"""
import sys
import numpy as np
from PIL import Image

sys.path.insert(0, __file__.rsplit("/", 1)[0])
from evaluate import srgb_to_lab, N_PX, R_DISK  # noqa: E402


def lab_to_srgb(lab):
    fy = (lab[..., 0] + 16) / 116
    f = np.stack([fy + lab[..., 1] / 500, fy, fy - lab[..., 2] / 200], -1)
    xyz = np.where(f > 0.2069, f ** 3, (f - 16 / 116) / 7.787) * np.array([0.9505, 1.0, 1.089])
    m = np.linalg.inv(np.array([[0.4124, 0.3576, 0.1805], [0.2126, 0.7152, 0.0722], [0.0193, 0.1192, 0.9505]]))
    c = np.clip(xyz @ m.T, 0, 1)
    return np.where(c <= 0.0031308, 12.92 * c, 1.055 * c ** (1 / 2.4) - 0.055)


def fmt(c):
    return "#%02x%02x%02x (%.2f %.2f %.2f)" % (*(np.round(c * 255).astype(int)), *c)


for path in sys.argv[1:]:
    img = np.asarray(Image.open(path).convert("RGB").resize((N_PX, N_PX), Image.NEAREST)).astype(float) / 255
    y, x = np.mgrid[:N_PX, :N_PX]
    m = np.hypot(x - N_PX / 2 + 0.5, y - N_PX / 2 + 0.5) < 0.55 * R_DISK
    m &= img.mean(-1) > 0.03
    lab = srgb_to_lab(img[m])
    mu = lab.mean(0)
    _, _, vt = np.linalg.svd(lab - mu, full_matrices=False)
    ax = vt[0] * np.sign(vt[0][0])
    t = (lab - mu) @ ax
    print(f"{path}: {m.sum()} px, line direction dL {ax[0]:.2f} da {ax[1]:.2f} db {ax[2]:.2f}")
    for q in (5, 50, 95):
        print(f"   p{q:<3d} {fmt(lab_to_srgb(mu + ax * np.percentile(t, q)))}")
    # k-means (3) in Lab
    rng = np.random.default_rng(0)
    cen = lab[rng.choice(len(lab), 3, replace=False)]
    for _ in range(30):
        lbl = np.argmin(((lab[:, None] - cen[None]) ** 2).sum(-1), 1)
        cen = np.array([lab[lbl == k].mean(0) if (lbl == k).any() else cen[k] for k in range(3)])
    for k in np.argsort(cen[:, 0]):
        print(f"   cluster {100 * (lbl == k).mean():4.0f}%  {fmt(lab_to_srgb(cen[k]))}")
