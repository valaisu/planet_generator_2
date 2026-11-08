"""Low-orbit views (--view leo: 400 km, horizon in the top quarter) of the first N
showcase picks (most distinct first, see showcase.py) + one labelled sheet.

    uv run --no-project --with pillow python tools/leo_showcase.py --n 20
    -> renders/leo_showcase/<seed>.png (+ _x4) and renders/leo_showcase/sheet.png
"""
import argparse
import json
import subprocess
from pathlib import Path

from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parent.parent


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--n", type=int, default=20)
    ap.add_argument("--showcase", default="showcase/showcase.json")
    ap.add_argument("--out", default="renders/leo_showcase")
    ap.add_argument("--cols", type=int, default=4)
    a = ap.parse_args()

    out = ROOT / a.out
    out.mkdir(parents=True, exist_ok=True)
    picks = json.loads((ROOT / a.showcase).read_text())[:a.n]
    for p in picks:
        print(f"#{p['seed']} [{p['type']}]", flush=True)
        subprocess.run([str(ROOT / "build/earthgen"), "--random", str(p["seed"]), "--view", "leo",
                        "--out", str(out / f"{p['seed']}.png")], stderr=subprocess.DEVNULL, check=True)

    tw, th, label = 512, 256, 18
    rows = (len(picks) + a.cols - 1) // a.cols
    sheet = Image.new("RGB", (a.cols * tw, rows * (th + label)), (24, 24, 28))
    d = ImageDraw.Draw(sheet)
    for i, p in enumerate(picks):
        x, y = (i % a.cols) * tw, (i // a.cols) * (th + label)
        sheet.paste(Image.open(out / f"{p['seed']}.png").convert("RGB").resize((tw, th), Image.NEAREST), (x, y))
        d.text((x + 4, y + th + 3), f"#{p['seed']}  {p['type']}", fill=(220, 220, 220))
    sheet.save(out / "sheet.png")
    print(f"-> {out / 'sheet.png'}")


if __name__ == "__main__":
    main()
