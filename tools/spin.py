"""Spinning animations of the first N showcase picks (the greedy diversity order of
showcase.py, so the first N are the most different from each other).

    uv run --no-project python tools/spin.py              # 10 planets -> renders/spin/<seed>.{gif,mp4}
    uv run --no-project python tools/spin.py --n 10 --turns 0.3 --seconds 8

The planet turns under a fixed sun (--spin), the starfield at --sky (fraction of the
planet's speed). Clouds advance at the planet's real pace (its rotation period from the
card), capped at --max-cloud-minutes per frame: locked worlds turn once per orbit
(weeks), which would make the weather race.
"""
import argparse
import json
import re
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--n", type=int, default=10)
    ap.add_argument("--showcase", default="showcase/showcase.json")
    ap.add_argument("--turns", type=float, default=0.3, help="planet turns per clip")
    ap.add_argument("--sky", type=float, default=0.067, help="starfield speed / planet speed")
    ap.add_argument("--seconds", type=float, default=8)
    ap.add_argument("--fps", type=int, default=24)
    ap.add_argument("--max-cloud-minutes", type=float, default=10)
    a = ap.parse_args()

    cards = json.loads((ROOT / "models/cards.json").read_text())
    frames = int(a.seconds * a.fps)
    for pick in json.loads((ROOT / a.showcase).read_text())[:a.n]:
        seed = pick["seed"]
        hours = float(re.search(r"rotation\s+([\d.e+-]+) h", cards[str(seed)]).group(1))
        minutes = min(a.max_cloud_minutes, hours * 60 * a.turns / frames)
        print(f"#{seed} [{pick['type']}]: rotation {hours:.1f} h, clouds {minutes:.1f} min/frame", flush=True)
        subprocess.run([str(ROOT / "tools/animate.sh"), f"renders/spin/{seed}", str(frames), str(a.fps),
                        "--random", str(seed), "--spin", "--orbits", str(a.turns), "--sky-speed", str(a.sky),
                        "--cloud-minutes", f"{minutes:.2f}"], cwd=ROOT, check=True)


if __name__ == "__main__":
    main()
