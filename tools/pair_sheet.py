"""Side-by-side sheet: every pixelized disk reference next to the planet generated
from its physics (earthgen --planet NAME, sun matched to the photos' phase), labelled.
    uv run --no-project --with pillow python tools/pair_sheet.py renders/pair_sheet.png
"""
import subprocess
import sys
import tempfile
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

# (reference image, label, preset it is compared with)
PAIRS = [
    ("references/earth_0_x4.png", "Earth (photo 0)", "earth"),
    ("references/earth_1_x4.png", "Earth (photo 1)", "earth"),
    ("references/earth_2_x4.png", "Earth (photo 2)", "earth"),
    ("references/earth_3_x4.png", "Earth (photo 3)", "earth"),
    ("references/solar/mercury_x4.png", "Mercury", "mercury"),
    ("references/solar/venus_x4.png", "Venus", "venus"),
    ("references/solar/mars_x4.png", "Mars", "mars"),
    ("references/solar/europa_x4.png", "Europa", "europa"),
    ("references/solar/titan_x4.png", "Titan", "titan"),
    ("references/solar/titan_infrared_x4.png", "Titan (infrared photo)", "titan"),
    ("references/solar/pluto_x4.png", "Pluto", "pluto"),
]
# Sun turned toward the camera for bodies photographed near full phase (as in
# tools/eval_all.sh), so lighting doesn't confound the comparison.
SUN_AZ = {"mars": 43, "europa": 30, "titan": 30, "pluto": 30}
TILE, GAP, LABEL, COLS = 256, 24, 28, 3
BG, FG, DIM = (12, 12, 16), (230, 230, 235), (150, 150, 160)


def main():
    out = Path(sys.argv[1] if len(sys.argv) > 1 else "pair_sheet.png")
    root = Path(__file__).resolve().parent.parent
    with tempfile.TemporaryDirectory() as tmp:
        gen = {}
        for preset in dict.fromkeys(p for _, _, p in PAIRS):
            png = Path(tmp) / f"{preset}.png"
            subprocess.run([str(root / "build/earthgen"), "--planet", preset, "--sun-az", str(SUN_AZ.get(preset, 0)), "--out", str(png)],
                           check=True, stderr=subprocess.DEVNULL)
            gen[preset] = Image.open(Path(tmp) / f"{preset}_x4.png").convert("RGB").resize((TILE, TILE), Image.NEAREST)

        rows = (len(PAIRS) + COLS - 1) // COLS
        cell_w, cell_h = 2 * TILE + GAP, TILE + LABEL
        sheet = Image.new("RGB", (COLS * cell_w + (COLS + 1) * GAP, rows * cell_h + (rows + 1) * GAP), BG)
        draw = ImageDraw.Draw(sheet)
        try:
            font = ImageFont.load_default(size=18)
        except TypeError:  # Pillow < 10.1
            font = ImageFont.load_default()
        for i, (ref, label, preset) in enumerate(PAIRS):
            x = GAP + (i % COLS) * (cell_w + GAP)
            y = GAP + (i // COLS) * (cell_h + GAP)
            sheet.paste(Image.open(root / ref).convert("RGB").resize((TILE, TILE), Image.NEAREST), (x, y + LABEL))
            sheet.paste(gen[preset], (x + TILE, y + LABEL))
            draw.text((x, y + 2), label, fill=FG, font=font)
            draw.text((x + TILE, y + 2), "generated", fill=DIM, font=font)
        sheet.save(out)
    print(f"wrote {out} (reference left, generated right)")


if __name__ == "__main__":
    main()
