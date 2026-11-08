"""N interesting new planets: liked (predicted from the user's tiers) AND different from
each other. Not only S-tier: anything predicted B or better can be picked; variety
decides among them.

    TORCH="uv run --no-project --index https://download.pytorch.org/whl/cu126 --with torch==2.14.1 --with torchvision --with pillow --with numpy --with scikit-learn"
    $TORCH python tools/showcase.py                       # 42 planets -> renders/showcase/showcase.png
    $TORCH python tools/showcase.py --n 42 --render 160 --first 20000 --diversity 0.65

1. Preference models trained on pool/ (latest tiering, S..D -> 4..0):
   card model = gradient-boosted trees on the --describe card (tools/prefs.py features);
   image model = ridge on frozen DINOv2 ViT-S/14 embeddings of the 128 px pixel-art PNGs
   (the images the user judged), cross-validated pairwise accuracy printed.
2. Seeds FIRST.. are described (fast); those the card model puts below --min-card are
   skipped (saves renders: Titan-like haze...); the first --render others are rendered
   into DIR (pool format: rank.py DIR can tier them).
3. The image model predicts each render's tier; candidates >= --min-tier are eligible.
4. Greedy MMR: start with the best; each next pick maximises
   (1 - diversity) x predicted tier (0..1 over the eligible) + diversity x distance to the
   nearest pick (cosine distance in DINO space, 0..1 over the candidates).
Output: DIR/showcase.json (picks with predicted tier) + renders/showcase/showcase.png.
Embeddings are cached in models/emb_pixel_vits14.npz (per file name + pool dir).
"""
import argparse
import json
import random
import sys
import tempfile
from pathlib import Path

import numpy as np
import torch
import torch.nn.functional as F
from PIL import Image, ImageDraw
from sklearn.ensemble import HistGradientBoostingRegressor
from sklearn.linear_model import RidgeCV
from sklearn.model_selection import KFold

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
from pool import render, world_type  # noqa: E402
from prefs import card_features, describe, matrix, pairwise_acc  # noqa: E402

MEAN, STD = torch.tensor((0.485, 0.456, 0.406)).view(1, 3, 1, 1), torch.tensor((0.229, 0.224, 0.225)).view(1, 3, 1, 1)
TIER = "DCBAS"


class Embedder:
    def __init__(self):
        self.device = "cuda" if torch.cuda.is_available() else "cpu"
        local = Path(torch.hub.get_dir()) / "facebookresearch_dinov2_main"  # (cached by dino.py; no GitHub check)
        self.model = (torch.hub.load(str(local), "dinov2_vits14", source="local") if local.exists() else
                      torch.hub.load("facebookresearch/dinov2", "dinov2_vits14")).to(self.device).eval()
        self.cache_f = ROOT / "models/emb_pixel_vits14.npz"
        self.cache = dict(np.load(self.cache_f)) if self.cache_f.exists() else {}

    @torch.no_grad()
    def __call__(self, paths):
        todo = [p for p in paths if self.key(p) not in self.cache]
        for i in range(0, len(todo), 32):
            x = torch.stack([torch.from_numpy(np.asarray(Image.open(p).convert("RGB"))).permute(2, 0, 1) for p in todo[i:i + 32]])
            x = F.interpolate(x.float() / 255, size=224, mode="bilinear", antialias=True)
            e = F.normalize(self.model(((x - MEAN) / STD).to(self.device)), dim=-1)
            for p, v in zip(todo[i:i + 32], e.cpu().numpy()):
                self.cache[self.key(p)] = v
        if todo:
            self.cache_f.parent.mkdir(exist_ok=True)
            np.savez(self.cache_f, **self.cache)
        return np.stack([self.cache[self.key(p)] for p in paths])

    @staticmethod
    def key(p):
        return f"{Path(p).parent.name}/{Path(p).name}"


def cv_acc(make, X, y):
    pred = np.zeros(len(y))
    for tr, te in KFold(5, shuffle=True, random_state=0).split(X):
        pred[te] = make().fit(X[tr], y[tr]).predict(X[te])
    return pairwise_acc(pred, y)


def trees():
    return HistGradientBoostingRegressor(max_depth=3, learning_rate=0.05, max_iter=200, min_samples_leaf=15,
                                         l2_regularization=1.0, random_state=0)


def ridge():
    return RidgeCV(alphas=np.logspace(-3, 3, 25))


def mmr(emb, pred, n, diversity):
    q = (pred - pred.min()) / max(1e-9, pred.max() - pred.min())
    dist_all = 1 - emb @ emb.T
    scale = max(1e-9, np.percentile(dist_all, 90))
    picks = [int(np.argmax(pred))]
    nearest = dist_all[picks[0]].copy()
    while len(picks) < min(n, len(pred)):
        value = (1 - diversity) * q + diversity * np.minimum(nearest / scale, 1.0)
        value[picks] = -np.inf
        k = int(np.argmax(value))
        picks.append(k)
        nearest = np.minimum(nearest, dist_all[k])
    return picks


def sheet(out_png, items, cols=7, tile=192):
    rows = (len(items) + cols - 1) // cols
    label = 30
    img = Image.new("RGB", (cols * tile, rows * (tile + label)), (24, 24, 28))
    d = ImageDraw.Draw(img)
    for i, (path, text1, text2) in enumerate(items):
        x, y = (i % cols) * tile, (i // cols) * (tile + label)
        img.paste(Image.open(path).convert("RGB").resize((tile, tile), Image.NEAREST), (x, y))
        d.text((x + 4, y + tile + 2), text1, fill=(230, 230, 230))
        d.text((x + 4, y + tile + 15), text2[:34], fill=(150, 150, 160))
    out_png.parent.mkdir(parents=True, exist_ok=True)
    img.save(out_png)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--n", type=int, default=42)
    ap.add_argument("--dir", default="showcase")
    ap.add_argument("--first", type=int, default=20000, help="first candidate seed (pool/views use < 6000)")
    ap.add_argument("--render", type=int, default=160, help="how many candidates to render")
    ap.add_argument("--min-card", type=float, default=1.5, help="skip seeds the card model puts below this (C=1, B=2)")
    ap.add_argument("--min-tier", type=float, default=2.0, help="eligible: image model tier >= this (B=2)")
    ap.add_argument("--diversity", type=float, default=0.65, help="0 = best predicted only, 1 = most different only")
    a = ap.parse_args()

    pool = json.loads((ROOT / "pool/index.json").read_text())
    log = [json.loads(l) for l in (ROOT / "pool/comparisons.jsonl").read_text().splitlines() if l.strip()]
    latest = [j for j in log if "tiers" in j][-1]
    tier_of = {s: 4 - i for i, t in enumerate(latest["tiers"]) for s in t}
    seeds = sorted(s for s in tier_of if str(s) in pool)
    y = np.array([tier_of[s] for s in seeds], float)

    cache_f = ROOT / "models/cards.json"
    cards = json.loads(cache_f.read_text()) if cache_f.exists() else {}
    Xc, keys = matrix([card_features(describe(s, cards)) for s in seeds])
    card_model = trees().fit(Xc, y)
    embed = Embedder()
    Xe = embed([ROOT / "pool" / pool[str(s)]["file"] for s in seeds])
    img_model = ridge().fit(Xe, y)
    print(f"trained on {len(seeds)} tiered planets; cross-validated pairwise accuracy: card trees "
          f"{cv_acc(trees, Xc, y):.3f}, pixel-art DINO ridge {cv_acc(ridge, Xe, y):.3f}", flush=True)

    out = ROOT / a.dir
    out.mkdir(exist_ok=True)
    index_f = out / "index.json"
    index = json.loads(index_f.read_text()) if index_f.exists() else {}
    s, skipped = a.first, 0
    todo = [int(k) for k in index]
    while len(todo) < a.render:
        if str(s) not in index:
            f = card_features(describe(s, cards))
            if card_model.predict(np.array([[f.get(k, 0.0) for k in keys]]))[0] >= a.min_card:
                todo.append(s)
            else:
                skipped += 1
        s += 1
    cache_f.write_text(json.dumps(cards))
    new = [t for t in todo if str(t) not in index]
    print(f"seeds {a.first}..{s - 1}: {skipped} skipped by the card model, rendering {len(new)} "
          f"(~{len(new) * 8 // 60} min)", flush=True)
    with tempfile.TemporaryDirectory() as tmp:
        for t in new:
            render(out, index, t, world_type(describe(t, cards)), tmp)
    cache_f.write_text(json.dumps(cards))

    cand = sorted(int(k) for k in index)
    emb = embed([out / index[str(c)]["file"] for c in cand])
    pred = img_model.predict(emb)
    ok = np.where(pred >= a.min_tier)[0]
    print(f"{len(ok)} of {len(cand)} candidates predicted >= {a.min_tier} ({TIER[min(4, int(a.min_tier))]})", flush=True)
    picks = [ok[i] for i in mmr(emb[ok], pred[ok], a.n, a.diversity)]

    chosen = [{"seed": cand[i], "predicted_tier": round(float(pred[i]), 2), **index[str(cand[i])]} for i in picks]
    (out / "showcase.json").write_text(json.dumps(chosen, indent=1))
    letter = lambda p: TIER[int(np.clip(round(p), 0, 4))]
    sheet(ROOT / "renders/showcase/showcase.png",
          [(out / c["file"], f"#{c['seed']}  {letter(c['predicted_tier'])} {c['predicted_tier']:.1f}", c["type"]) for c in chosen])
    types = {}
    for c in chosen:
        types[c["type"]] = types.get(c["type"], 0) + 1
    print(f"picked {len(chosen)}: predicted " + " ".join(f"{t} {sum(letter(c['predicted_tier']) == t for c in chosen)}" for t in "SABCD"))
    print("types: " + ", ".join(f"{k} {v}" for k, v in sorted(types.items(), key=lambda kv: -kv[1])))
    print(f"-> renders/showcase/showcase.png, {out / 'showcase.json'}")


if __name__ == "__main__":
    main()
