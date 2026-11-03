"""Rank knob settings of one body by how close they come to its reference photos
on the measures that matter (tools/evaluate.py): detail per scale (log ratios of
band1..band4 + regional), feature sparsity (kurtosis), brightness/chroma spread
and mean colour. The metric only narrows the range: the user picks from a
variants sheet of the top settings.
    uv run --no-project --with numpy --with pillow --with scipy python tools/tune.py mars \\
        --refs references/solar/mars.png references/extra/mars_osiris.png --base "--sun-az 43" \\
        --grid "dark_mix=0.6,0.8,1" --grid "sand_lows=0.4,0.8" [--top 8]
Paletted references + paletted renders (the final look) by default; --raw compares
references/raw/* against earthgen --raw.
"""
import argparse
import itertools
import shlex
import subprocess
import sys
import tempfile
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).parent))
from evaluate import analyse  # noqa: E402

BANDS = ["band1", "band2", "band3", "band4", "regional"]


def distance(m, ref):
    d = sum(np.log(max(m[k], 1e-4) / ref[k]) ** 2 for k in BANDS)
    d += np.log(m["sdL"] / ref["sdL"]) ** 2 + 0.5 * np.log(max(m["sdC"], 0.1) / ref["sdC"]) ** 2
    d += ((m["L"] - ref["L"]) / 10) ** 2 + ((m["a"] - ref["a"]) / 8) ** 2 + ((m["b"] - ref["b"]) / 8) ** 2
    d += 0.3 * ((np.tanh(m["kurt2"] / 10) - np.tanh(ref["kurt2"] / 10)) ** 2 + (np.tanh(m["kurt3"] / 10) - np.tanh(ref["kurt3"] / 10)) ** 2)
    return float(d)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("body")
    ap.add_argument("--refs", nargs="+", required=True)
    ap.add_argument("--base", default="")
    ap.add_argument("--grid", action="append", default=[])
    ap.add_argument("--raw", action="store_true")
    ap.add_argument("--top", type=int, default=8)
    a = ap.parse_args()
    root = Path(__file__).resolve().parent.parent
    refs = [analyse(r)[0] for r in a.refs]
    ref = {k: float(np.mean([r[k] for r in refs])) for k in refs[0]}
    axes = [(g.split("=", 1)[0], g.split("=", 1)[1].split(",")) for g in a.grid] or [("shading", ["1"])]
    results = []
    with tempfile.TemporaryDirectory() as tmp:
        for combo in itertools.product(*[v for _, v in axes]):
            knobs = ",".join(f"{k}={v}" for (k, _), v in zip(axes, combo))
            png = Path(tmp) / "r.png"
            cmd = [str(root / "build/earthgen"), "--planet", a.body, *shlex.split(a.base), "--world", knobs, "--out", str(png)]
            if a.raw:
                cmd.append("--raw")
            subprocess.run(cmd, check=True, stderr=subprocess.DEVNULL)
            m = analyse(png)[0]
            results.append((distance(m, ref), knobs, m))
            print(f"{results[-1][0]:7.3f}  {knobs}", flush=True)
    results.sort(key=lambda r: r[0])
    print(f"\nreference mean: " + " ".join(f"{k} {ref[k]:.3f}" for k in BANDS + ["sdL", "sdC", "L", "a", "b", "kurt2"]))
    print("best:")
    for d, knobs, m in results[:a.top]:
        print(f"{d:7.3f}  {knobs:40s} " + " ".join(f"{k} {m[k]:.3f}" for k in BANDS + ["sdL", "sdC", "L", "a", "b", "kurt2"]))


if __name__ == "__main__":
    main()
