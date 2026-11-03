"""Compare renders against reference photos of the same body, numbers + a figure.

All images are 128 px disks in earthgen's framing (stylize_ref output). Only the
lit disk is used (away from limb and terminator). Lighting is divided out:
D = log(luma) - log(blur(luma, 12 px)), which removes shading gradients and keeps
surface variation below ~12 px; a coarser ratio gives the regional variation.

Per image:
  band1..band4   RMS of D in difference-of-Gaussian bands ~1, 2, 4, 8 px
                 ("how much detail at each size")
  regional       RMS of log(blur 3 px / blur 24 px)  (8-24 px structures)
  kurt2, kurt3   excess kurtosis of bands 2, 3: ~0 = uniform Gaussian noise,
                 >> 0 = sparse distinct features on a calm background
  L, a, b        mean CIELAB colour;  sdL = brightness spread
  chroma, sdC    mean / spread of chroma
  hue_var        a*/b* scatter left after removing the part predicted by L*
                 (several colour families at equal brightness vs one ramp)
    uv run --no-project --with numpy --with pillow --with scipy --with matplotlib \\
        python tools/evaluate.py OUT.png --ref references/raw/mars.png references/raw/mars_osiris.png \\
        --render mars_raw.png [--render other.png ...]
"""
import argparse
from pathlib import Path

import numpy as np
from PIL import Image
from scipy import ndimage

N_PX = 128
R_DISK = 0.84 * N_PX / 2


def srgb_to_lab(rgb):
    c = np.where(rgb <= 0.04045, rgb / 12.92, ((rgb + 0.055) / 1.055) ** 2.4)
    m = np.array([[0.4124, 0.3576, 0.1805], [0.2126, 0.7152, 0.0722], [0.0193, 0.1192, 0.9505]])
    xyz = c @ m.T / np.array([0.9505, 1.0, 1.089])
    f = np.where(xyz > 0.008856, np.cbrt(xyz), 7.787 * xyz + 16 / 116)
    return np.stack([116 * f[..., 1] - 16, 500 * (f[..., 0] - f[..., 1]), 200 * (f[..., 1] - f[..., 2])], -1)


def analyse(path):
    img = np.asarray(Image.open(path).convert("RGB").resize((N_PX, N_PX), Image.NEAREST)).astype(float) / 255
    luma = img @ [0.299, 0.587, 0.114]
    y, x = np.mgrid[:N_PX, :N_PX]
    r = np.hypot(x - N_PX / 2 + 0.5, y - N_PX / 2 + 0.5)
    disk = r < R_DISK - 1
    big = ndimage.gaussian_filter(np.where(disk, luma, 0), 6) / np.maximum(ndimage.gaussian_filter(disk * 1.0, 6), 1e-3)
    # lit = not in shadow; a low threshold keeps genuinely dark terrain (Pluto's belt)
    lit = disk & (big > 0.15 * np.percentile(big[disk], 98))
    lit = ndimage.binary_erosion(lit, iterations=4)  # away from limb / terminator
    lg = np.log(np.maximum(luma, 0.01))

    def gblur(a, s):  # blur inside the disk only (normalised convolution)
        w = ndimage.gaussian_filter(disk * 1.0, s)
        return ndimage.gaussian_filter(np.where(disk, a, 0), s) / np.maximum(w, 1e-3)

    sig = [0.5, 1, 2, 4, 8]
    g = [gblur(lg, s) for s in sig]
    bands = [g[i] - g[i + 1] for i in range(4)]
    D = lg - gblur(lg, 12)
    regional = gblur(lg, 3) - gblur(lg, 24)
    lab = srgb_to_lab(img)[lit]
    L, a, b = lab[:, 0], lab[:, 1], lab[:, 2]
    C = np.hypot(a, b)
    A = np.c_[L, np.ones_like(L)]
    res = [b_ - A @ np.linalg.lstsq(A, b_, rcond=None)[0] for b_ in (a, b)]

    def kurt(v):
        v = v - v.mean()
        return float((v ** 4).mean() / max((v ** 2).mean() ** 2, 1e-12) - 3)

    m = {f"band{i + 1}": float(np.sqrt((bands[i][lit] ** 2).mean())) for i in range(4)}
    m.update(regional=float(np.sqrt((regional[lit] ** 2).mean())), kurt2=kurt(bands[1][lit]), kurt3=kurt(bands[2][lit]),
             L=float(L.mean()), a=float(a.mean()), b=float(b.mean()), sdL=float(L.std()),
             chroma=float(C.mean()), sdC=float(C.std()), hue_var=float(np.sqrt(res[0].var() + res[1].var())),
             lit=float(lit.mean()))
    return m, dict(img=img, D=np.where(lit, D, np.nan), lab=lab, rgb=img[lit])


KEYS = ["band1", "band2", "band3", "band4", "regional", "kurt2", "kurt3", "L", "a", "b", "sdL", "chroma", "sdC", "hue_var"]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("out")
    ap.add_argument("--ref", nargs="+", required=True)
    ap.add_argument("--render", nargs="+", required=True)
    a = ap.parse_args()
    rows = [("ref", p, *analyse(p)) for p in a.ref] + [("render", p, *analyse(p)) for p in a.render]

    print(f"{'':7s} {'image':28s} " + " ".join(f"{k:>7s}" for k in KEYS))
    for kind, p, m, _ in rows:
        print(f"{kind:7s} {Path(p).name[:28]:28s} " + " ".join(f"{m[k]:7.3f}" if abs(m[k]) < 100 else f"{m[k]:7.1f}" for k in KEYS))
    refm = {k: np.mean([m[k] for kind, _, m, _ in rows if kind == "ref"]) for k in KEYS}
    for kind, p, m, _ in rows:
        if kind == "render":
            ratio = {k: m[k] / refm[k] for k in KEYS[:5] + ["sdL", "sdC", "hue_var"]}
            diff = {k: m[k] - refm[k] for k in ["L", "a", "b", "chroma", "kurt2", "kurt3"]}
            print(f"vs refs  {Path(p).name[:28]:28s} ratio: " + " ".join(f"{k} {v:.2f}" for k, v in ratio.items())
                  + " | diff: " + " ".join(f"{k} {v:+.1f}" for k, v in diff.items()))

    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    n = len(rows)
    fig, ax = plt.subplots(4, n, figsize=(2.6 * n, 10.4), squeeze=False)
    for j, (kind, p, m, d) in enumerate(rows):
        ax[0, j].imshow(d["img"], interpolation="nearest")
        ax[0, j].set_title(f"{kind}: {Path(p).stem[:18]}", fontsize=8)
        ax[1, j].imshow(d["D"], cmap="gray", vmin=-0.25, vmax=0.25, interpolation="nearest")
        ax[1, j].set_title("detail (lighting removed)", fontsize=7)
        ax[2, j].scatter(d["lab"][:, 1], d["lab"][:, 2], c=np.clip(d["rgb"], 0, 1), s=3)
        ax[2, j].set_xlim(-20, 40); ax[2, j].set_ylim(-20, 50)
        ax[2, j].set_title("a* vs b* (colour spread)", fontsize=7)
        ax[3, j].scatter(d["lab"][:, 0], np.hypot(d["lab"][:, 1], d["lab"][:, 2]), c=np.clip(d["rgb"], 0, 1), s=3)
        ax[3, j].set_xlim(0, 100); ax[3, j].set_ylim(0, 50)
        ax[3, j].set_title("L* vs chroma", fontsize=7)
        for i in range(2):
            ax[i, j].axis("off")
    fig.tight_layout()
    fig.savefig(a.out, dpi=80)
    # Detail spectrum plot, all images on one axis.
    fig2, axs = plt.subplots(1, 1, figsize=(5, 3.5))
    for kind, p, m, _ in rows:
        axs.plot([1, 2, 4, 8, 16], [m["band1"], m["band2"], m["band3"], m["band4"], m["regional"]],
                 "o-" if kind == "render" else "s--", label=f"{kind} {Path(p).stem[:16]}",
                 color=None if kind == "render" else "k", alpha=1 if kind == "render" else 0.5)
    axs.set_xscale("log", base=2); axs.set_xlabel("feature size (px)"); axs.set_ylabel("RMS log-brightness")
    axs.legend(fontsize=6); fig2.tight_layout()
    fig2.savefig(str(Path(a.out).with_suffix("")) + "_spectrum.png", dpi=90)
    print(f"wrote {a.out} and *_spectrum.png")


if __name__ == "__main__":
    main()
