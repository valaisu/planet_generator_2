"""Contact sheet of earthgen variants for the user to pick from (see the
visual-options skill): every tile = the base args + one variant's args, labelled
with its ID and the args; optional reference tile first.
    uv run --no-project --with pillow python tools/variants.py OUT.png --base "--planet mars" \\
        --ref references/solar/mars_x4.png \\
        --var "A=--world scarps=0" --var "B=--world scarps=1" --var "C=--world scarps=2" [--cols 4]
A variant "ID=@path.png" is an image tile (e.g. each row's reference). A variant can also be a grid: --grid "scarps=0,1,2" --grid "streaks=0,1" (all combinations
of --world knobs, IDs A1, A2, ...).
"""
import argparse
import itertools
import shlex
import string
import subprocess
import tempfile
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

TILE, LABEL, GAP = 256, 34, 12


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("out")
    ap.add_argument("--base", default="")
    ap.add_argument("--var", action="append", default=[], help="ID=earthgen args")
    ap.add_argument("--grid", action="append", default=[], help="knob=v1,v2,... (combined over all --grid)")
    ap.add_argument("--ref", help="reference image for the first tile")
    ap.add_argument("--cols", type=int, default=4)
    ap.add_argument("--tile", type=int, default=TILE, help="tile size; above 256 the unscaled render is used (e.g. --size 800 --raw)")
    a = ap.parse_args()

    variants = [tuple(v.split("=", 1)) for v in a.var]
    if a.grid:
        axes = [(g.split("=", 1)[0], g.split("=", 1)[1].split(",")) for g in a.grid]
        for i, combo in enumerate(itertools.product(*[vals for _, vals in axes])):
            knobs = ",".join(f"{k}={v}" for (k, _), v in zip(axes, combo))
            row, col = divmod(i, len(axes[-1][1]))
            variants.append((f"{string.ascii_uppercase[row]}{col + 1}", f"--world {knobs}"))

    root = Path(__file__).resolve().parent.parent
    try:
        font = ImageFont.load_default(size=13)
    except TypeError:  # Pillow < 10.1
        font = ImageFont.load_default()
    tiles = []
    if a.ref:
        tiles.append(("reference", Path(a.ref).name, Image.open(a.ref).convert("RGB")))
    with tempfile.TemporaryDirectory() as tmp:
        for vid, args in variants:
            if args.startswith("@"):  # an image tile (e.g. a row's reference)
                tiles.append((vid, Path(args[1:]).name, Image.open(root / args[1:]).convert("RGB")))
                continue
            png = Path(tmp) / f"{vid}.png"
            cmd = [str(root / "build/earthgen"), *shlex.split(a.base), *shlex.split(args), "--out", str(png)]
            subprocess.run(cmd, check=True, stderr=subprocess.DEVNULL)
            tiles.append((vid, args, Image.open(Path(tmp) / (f"{vid}.png" if a.tile > TILE else f"{vid}_x4.png")).convert("RGB")))

    cols = min(a.cols, len(tiles))
    rows = (len(tiles) + cols - 1) // cols
    T = a.tile
    sheet = Image.new("RGB", (cols * (T + GAP) + GAP, rows * (T + LABEL + GAP) + GAP), (12, 12, 16))
    draw = ImageDraw.Draw(sheet)
    for i, (vid, args, im) in enumerate(tiles):
        x = GAP + (i % cols) * (T + GAP)
        y = GAP + (i // cols) * (T + LABEL + GAP)
        sheet.paste(im.resize((T, T), Image.NEAREST if T <= TILE else Image.LANCZOS), (x, y + LABEL))
        draw.text((x, y), vid, fill=(240, 240, 245), font=font)
        draw.text((x, y + 16), args.replace("--world ", "")[:40], fill=(150, 150, 160), font=font)
    sheet.save(a.out)
    print(f"wrote {a.out} ({len(variants)} variants)")


if __name__ == "__main__":
    main()
