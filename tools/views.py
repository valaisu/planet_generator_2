"""Training data for DINOv2 fine-tuning (tools/dino.py): several views of each random
planet, palette-free (--raw) at 224 px (16x16 patches of 14 px). The views of one
planet are positives for each other: one simulation, 4 frames over 67 deg of yaw, the
sun swinging +-34 deg around the usual angle, the weather advancing 3 h per frame.
    uv run --no-project python tools/views.py views 1 6000 40     # DIR FIRST COUNT PER_TYPE
Seeds of pool/ come first (so the judged planets get embeddings), then seeds
FIRST..FIRST+COUNT-1 round-robin over world types (up to PER_TYPE each), so stopping
at any time leaves a balanced set. Incremental: DIR/<seed>/f000k.png + DIR/index.json.
"""
import json
import shutil
import subprocess
import sys
import tempfile
from collections import defaultdict
from pathlib import Path

from pool import ROOT, describe, world_type

VIEW_ARGS = ["--size", "224", "--raw", "--scale", "1", "--frames", "4", "--orbits", "0.1875",
             "--sun-az", "33.75", "--cloud-minutes", "180"]


def render(out, index, seed, kind, tmp):
    run = subprocess.run([str(ROOT / "build/earthgen"), "--random", str(seed), *VIEW_ARGS,
                          "--out", str(Path(tmp) / "v.png")], stderr=subprocess.PIPE, text=True)
    frames = Path(tmp) / "v_frames"
    if run.returncode != 0 or not frames.is_dir():
        print(f"#{seed}: failed ({run.returncode})", flush=True)
        return
    card = next((l[6:] for l in run.stderr.splitlines() if l.startswith("card: ")), "")
    dest = out / str(seed)
    shutil.rmtree(dest, ignore_errors=True)
    shutil.move(str(frames), dest)
    index[str(seed)] = {"views": sorted(p.name for p in dest.glob("f*.png")), "card": card, "type": kind}
    tmp_index = (out / "index.json").with_suffix(".tmp")
    tmp_index.write_text(json.dumps(index, indent=1))
    tmp_index.replace(out / "index.json")
    print(f"#{seed} [{kind}] ({len(index)})", flush=True)


def main():
    out = Path(sys.argv[1])
    first, count, per_type = (int(a) for a in sys.argv[2:5])
    out.mkdir(parents=True, exist_ok=True)
    index_file = out / "index.json"
    index = json.loads(index_file.read_text()) if index_file.exists() else {}
    pool_index = ROOT / "pool/index.json"
    todo = []
    if pool_index.exists():
        todo += [(int(s), e["type"]) for s, e in json.loads(pool_index.read_text()).items()]
    queues = defaultdict(list)
    for s in range(first, first + count):
        queues[world_type(describe(s))].append(s)
    have = defaultdict(int)
    for s, k in todo:
        have[k] += 1
    queued = {s for s, _ in todo}
    while any(queues.values()):  # round-robin over types
        for k in list(queues):
            while queues[k] and queues[k][0] in queued:
                queues[k].pop(0)
            if queues[k] and have[k] < per_type:
                todo.append((queues[k].pop(0), k))
                have[k] += 1
            else:
                queues[k] = []
    todo = [(s, k) for s, k in todo if str(s) not in index]
    print(f"rendering {len(todo)} planets", flush=True)
    with tempfile.TemporaryDirectory() as tmp:
        for s, k in todo:
            render(out, index, s, k, tmp)


if __name__ == "__main__":
    main()
