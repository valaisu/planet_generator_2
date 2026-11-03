"""Colour transfer prototype: the render gives the structure (what is darker /
lighter, redder / browner), the photo gives the colours. Every disk pixel takes
the photo's brightness at the same brightness rank, then, within that
brightness band, the photo's colour at the same rank along the band's main
colour direction (so the joint brightness/colour distribution, area shares
included, is the photo's). Background (space, stars) is left as rendered.
    uv run --no-project --with numpy --with pillow python tools/color_transfer.py \\
        RENDER.png PHOTO.jpg OUT.png        # render: earthgen --raw --vibrance 1 (disk framing)
"""
import sys

import numpy as np
from PIL import Image

sys.path.insert(0, __file__.rsplit("/", 1)[0])
from evaluate import srgb_to_lab  # noqa: E402

BINS = 48


def lab_to_srgb(lab):  # (as in endmembers.py, which runs on import)
    fy = (lab[..., 0] + 16) / 116
    f = np.stack([fy + lab[..., 1] / 500, fy, fy - lab[..., 2] / 200], -1)
    xyz = np.where(f > 0.2069, f ** 3, (f - 16 / 116) / 7.787) * np.array([0.9505, 1.0, 1.089])
    m = np.linalg.inv(np.array([[0.4124, 0.3576, 0.1805], [0.2126, 0.7152, 0.0722], [0.0193, 0.1192, 0.9505]]))
    c = np.clip(xyz @ m.T, 0, 1)
    return np.where(c <= 0.0031308, 12.92 * c, 1.055 * c ** (1 / 2.4) - 0.055)


INNER = 0.9  # statistics from inside this fraction of the radius (the photo's limb haze is whitish)


def disk_render(h, w):
    y, x = np.mgrid[:h, :w]
    r = np.hypot(x - (w - 1) / 2, y - (h - 1) / 2) / (0.84 * w / 2)  # earthgen framing (PLANET_FRAC)
    return r < 1.0 - 1.5 / (0.84 * w / 2), r  # minus the anti-aliased edge


def disk_photo(rgb):
    m = rgb.max(-1) > 0.06  # black space around the disk
    y, x = np.nonzero(m)
    cy, cx, rad = y.mean(), x.mean(), np.sqrt(m.sum() / np.pi)
    yy, xx = np.mgrid[:rgb.shape[0], :rgb.shape[1]]
    return m, np.hypot(xx - cx, yy - cy) / rad


def smooth(v, k):
    if k <= 1 or len(v) < k:
        return v
    c = np.cumsum(np.concatenate([[0.0], v]))
    out = (c[k:] - c[:-k]) / k
    pad = k // 2
    return np.concatenate([np.full(pad, out[0]), out, np.full(len(v) - len(out) - pad, out[-1])])


def main():
    render_p, photo_p, out_p = sys.argv[1:4]
    ren = np.asarray(Image.open(render_p).convert("RGB")).astype(float) / 255
    pho = np.asarray(Image.open(photo_p).convert("RGB")).astype(float) / 255
    mr, rr = disk_render(*ren.shape[:2])
    mp, rp = disk_photo(pho)
    rl = srgb_to_lab(ren[mr])
    inner_r = rr[mr] < INNER
    pl = srgb_to_lab(pho[mp & (rp < INNER)])
    rng = np.random.default_rng(0)
    jit = 1e-4 * rng.standard_normal(len(rl))

    # brightness: quantile within the render's interior -> the photo's brightness there
    ref_L = np.sort(rl[inner_r, 0] + jit[inner_r])
    q_r = np.clip(np.searchsorted(ref_L, rl[:, 0] + jit) / len(ref_L), 0.0, 1.0)
    p_L = np.sort(pl[:, 0])
    out = np.empty_like(rl)
    out[:, 0] = np.interp(q_r, (np.arange(len(p_L)) + 0.5) / len(p_L), p_L)

    # colour within each brightness band: rank along the photo band's main colour
    # direction (fitted on the interior, applied to every disk pixel)
    p_order = np.argsort(pl[:, 0])
    band = np.minimum((q_r * BINS).astype(int), BINS - 1)
    for b in range(BINS):
        pi = p_order[int(b / BINS * len(pl)):int((b + 1) / BINS * len(pl))]
        ab_p = pl[pi, 1:]
        d = np.linalg.svd(ab_p - ab_p.mean(0), full_matrices=False)[2][0]
        if d[0] < 0:
            d = -d  # +direction = redder, for the render and photo alike
        ri = np.nonzero(band == b)[0]
        key = rl[ri, 1:] @ d + jit[ri]
        ref = np.sort(key[inner_r[ri]]) if inner_r[ri].any() else np.sort(key)
        q = np.clip(np.searchsorted(ref, key) / len(ref), 0.0, 1.0)
        ps = ab_p[np.argsort(ab_p @ d)]
        k = max(5, len(ps) // 20)
        x = (np.arange(len(ps)) + 0.5) / len(ps)
        out[ri, 1] = np.interp(q, x, smooth(ps[:, 0], k))
        out[ri, 2] = np.interp(q, x, smooth(ps[:, 1], k))

    res = ren.copy()
    res[mr] = lab_to_srgb(out)
    Image.fromarray((np.clip(res, 0, 1) * 255 + 0.5).astype(np.uint8)).save(out_p)
    print(f"wrote {out_p} ({len(rl)} disk px; photo statistics from {len(pl)} px inside {INNER} R)")


if __name__ == "__main__":
    main()
