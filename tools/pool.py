"""Pre-render a pool of random planets for tools/rank.py: earthgen --random SEED, one
128 px pixel-art PNG per planet in DIR + DIR/index.json (seed, card, world type).
Incremental: seeds already in the pool are skipped, so it can be stopped and resumed
(~8 s per planet, ~450 per hour). Every planet is reproducible from its seed.
    uv run --no-project python tools/pool.py pool 1 2000             # DIR FIRST COUNT: a seed range
    uv run --no-project python tools/pool.py pool --balanced 8 1 3000
Balanced: reads the physics card (--describe, fast) of seeds FIRST..FIRST+COUNT-1, sorts
them into world types (world_type below) and renders up to N per type (types already in
the pool count), so rare types (magma oceans, lava, snowballs...) show up as often as
common ones. The planets are unchanged, only which seeds go into the pool.
    uv run --no-project python tools/pool.py --census 1 3000         # how common each type is
    uv run --no-project python tools/pool.py pool --from views/index.json   # the planets of another index (views.py)
"""
import json
import re
import subprocess
import sys
import tempfile
from collections import Counter
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent


def describe(seed):
    run = subprocess.run([str(ROOT / "build/earthgen"), "--random", str(seed), "--describe"],
                         stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    return run.stdout


def world_type(t):
    """World type from a --describe card (what you would see, roughly)."""
    line = lambda key: next((l for l in t.splitlines() if l.startswith(key)), "")
    surf, aero, liq = line("surface"), line("aerosols"), line("liquids")
    deck = "deck" in aero or "thick orange tholin haze" in aero
    if "magma ocean" in t and "young crust" not in t:
        return "magma ocean under steam" if deck else "magma ocean"
    if "young crust" in t:
        return "young crust"
    if "sulfuric" in aero: return "sulfuric deck (Venus-like)"
    if "thick orange tholin haze" in aero: return "tholin haze (Titan-like)"
    if deck: return "steam deck"
    if "Io-like" in t: return "Io-like"
    if "glowing lava" in surf: return "lava (scorching day side)"
    if "frozen over" in liq: return "frozen ocean"
    if liq.startswith("liquids    water"):
        return "living ocean world" if "vegetation" in surf else "ocean world"
    if "water-ice bedrock" in surf:
        if "N2 ice" in surf: return "N2 ice (Pluto-like)"
        if "salts" in surf or "brine" in surf: return "active ice shell"
        if "dark lag" in surf: return "old dark ice"
        if "tholin" in surf: return "tholin ice"
        return "clean ice"
    m = re.search(r"rust: ([a-z]+)", surf)
    if m: return {"hematite": "rust: hematite", "goethite": "rust: goethite"}.get(m.group(1), "rust: grey-brown")
    m = re.search(r"ice cover (\d+)%", liq)
    if m and int(m.group(1)) > 50: return "icy rock"
    if "space-weathered" in surf: return "grey airless"
    return "basalt"


def render(pool, index, seed, kind, tmp):
    png = Path(tmp) / f"r{seed}.png"
    run = subprocess.run([str(ROOT / "build/earthgen"), "--random", str(seed), "--out", str(png)],
                         stderr=subprocess.PIPE, text=True)
    if run.returncode != 0:
        print(f"#{seed}: failed ({run.returncode})", flush=True)
        return
    card = next((l[6:] for l in run.stderr.splitlines() if l.startswith("card: ")), "")
    png.replace(pool / f"{seed}.png")
    index[str(seed)] = {"file": f"{seed}.png", "card": card, "type": kind}
    tmp_index = (pool / "index.json").with_suffix(".tmp")  # write-then-rename: rank.py may read it meanwhile
    tmp_index.write_text(json.dumps(index, indent=1))
    tmp_index.replace(pool / "index.json")
    print(f"#{seed} [{kind}]: {card}", flush=True)


def main():
    args = sys.argv[1:]
    if args and args[0] == "--census":
        first, count = int(args[1]), int(args[2])
        c = Counter(world_type(describe(s)) for s in range(first, first + count))
        for k, n in c.most_common():
            print(f"{n:6d} {100 * n / count:5.1f}%  {k}")
        return
    pool = Path(args[0] if args else "pool")
    per_type = None
    if len(args) > 2 and args[1] == "--from":  # same planets as another index (types already known)
        src = json.loads(Path(args[2]).read_text())
        pool.mkdir(parents=True, exist_ok=True)
        index_file = pool / "index.json"
        index = json.loads(index_file.read_text()) if index_file.exists() else {}
        todo = [(int(s), e["type"]) for s, e in src.items() if s not in index]
        print(f"rendering {len(todo)} planets", flush=True)
        with tempfile.TemporaryDirectory() as tmp:
            for s, k in todo:
                render(pool, index, s, k, tmp)
        print(f"pool {pool}: {len(index)} planets")
        return
    if len(args) > 1 and args[1] == "--balanced":
        per_type = int(args[2])
        args = args[:1] + args[3:]
    first = int(args[1]) if len(args) > 1 else 1
    count = int(args[2]) if len(args) > 2 else 100
    pool.mkdir(parents=True, exist_ok=True)
    index_file = pool / "index.json"
    index = json.loads(index_file.read_text()) if index_file.exists() else {}
    for s, e in index.items():  # (pools from before world types)
        if "type" not in e:
            e["type"] = world_type(describe(int(s)))
    seeds = [s for s in range(first, first + count) if str(s) not in index]
    if per_type is not None:
        have = Counter(e["type"] for e in index.values())
        todo = []
        for s in seeds:
            k = world_type(describe(s))
            if have[k] < per_type:
                have[k] += 1
                todo.append((s, k))
        print(f"rendering {len(todo)} planets: " + ", ".join(f"{k} {n}" for k, n in sorted(have.items())), flush=True)
    else:
        todo = [(s, None) for s in seeds]
    with tempfile.TemporaryDirectory() as tmp:
        for s, k in todo:
            render(pool, index, s, k or world_type(describe(s)), tmp)
    print(f"pool {pool}: {len(index)} planets")


if __name__ == "__main__":
    main()
