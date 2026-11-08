"""Sheet of new planets: earthgen --random SEED for a range of seeds, each labelled
with its seed and a short physics card (mass, radius, light, T, pressure, liquid).
    uv run --no-project --with pillow python tools/random_sheet.py renders/random_sheet.png 1 24
Each tile is also saved to OUT_tiles/ (+ index.json) for tools/picker.py.
Or a list of seeds (+ optional extra earthgen arguments):
    uv run --no-project --with pillow python tools/random_sheet.py out.png 39,97,245 "--world lava_cracks=0.4"
"""
import json
import shlex
import subprocess
import sys
import tempfile
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

TILE, GAP, LABEL, COLS = 256, 16, 44, 6
BG, FG, DIM = (12, 12, 16), (230, 230, 235), (150, 150, 160)


def main():
    out = Path(sys.argv[1] if len(sys.argv) > 1 else "random_sheet.png")
    root = Path(__file__).resolve().parent.parent
    extra = []
    if len(sys.argv) > 2 and "," in sys.argv[2]:
        seeds = [int(x) for x in sys.argv[2].split(",")]
        extra = shlex.split(sys.argv[3]) if len(sys.argv) > 3 else []
    else:
        first = int(sys.argv[2]) if len(sys.argv) > 2 else 1
        seeds = range(first, first + (int(sys.argv[3]) if len(sys.argv) > 3 else 24))
    count = len(seeds)
    rows = (count + COLS - 1) // COLS
    sheet = Image.new("RGB", (COLS * (TILE + GAP) + GAP, rows * (TILE + LABEL + GAP) + GAP), BG)
    draw = ImageDraw.Draw(sheet)
    try:
        font = ImageFont.load_default(size=13)
    except TypeError:  # Pillow < 10.1
        font = ImageFont.load_default()
    tiles_dir = out.with_name(out.stem + "_tiles")
    tiles_dir.mkdir(parents=True, exist_ok=True)
    index = []
    with tempfile.TemporaryDirectory() as tmp:
        for i, seed in enumerate(seeds):
            png = Path(tmp) / f"r{seed}.png"
            run = subprocess.run([str(root / "build/earthgen"), "--random", str(seed), "--out", str(png)] + extra,
                                 check=True, stderr=subprocess.PIPE, text=True)
            card = next((l[6:] for l in run.stderr.splitlines() if l.startswith("card: ")), "")
            parts = card.split(", ")
            x = GAP + (i % COLS) * (TILE + GAP)
            y = GAP + (i // COLS) * (TILE + LABEL + GAP)
            draw.text((x, y), f"#{seed}  " + ", ".join(parts[:3]), fill=FG, font=font)
            draw.text((x, y + 18), ", ".join(parts[3:]), fill=DIM, font=font)
            img = Image.open(Path(tmp) / f"r{seed}_x4.png").convert("RGB").resize((TILE, TILE), Image.NEAREST)
            sheet.paste(img, (x, y + LABEL))
            img.save(tiles_dir / f"{seed}.png")
            index.append({"id": f"#{seed}", "label": card, "file": f"{seed}.png"})
            print(f"#{seed}: {card}", flush=True)
    sheet.save(out)
    (tiles_dir / "index.json").write_text(json.dumps(index, indent=1))
    print(f"wrote {out}")


if __name__ == "__main__":
    main()
