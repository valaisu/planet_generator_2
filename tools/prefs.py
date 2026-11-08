"""What predicts the user's tiers? Compares feature sets on the pool's latest tier
judgment (S..D -> 4..0), 5-fold cross-validation repeated 5 times. Score = pairwise
accuracy of the out-of-fold predictions: of all planet pairs in different tiers, the
share the model orders correctly (0.5 = chance; ties count half).

    uv run --no-project --with numpy --with scikit-learn python tools/prefs.py

Feature sets: world type (mean tier per type), generation inputs (the ~14 random
numbers of random_inputs, from the --describe "inputs" line), the physics card
(numbers + flags parsed from --describe), DINOv2 embeddings (frozen / fine-tuned,
models/embeddings_*.npz, mean of the 4 views). Tabular sets: gradient-boosted trees
and ridge regression, the better one is used; embeddings: ridge. "ensemble" averages
the out-of-fold predictions of the best tabular and the best embedding model.
Noise ceiling: planets tiered twice (an earlier judgment), how often the two agree.
Results also go to models/prefs_results.txt.
"""
import json
import math
import re
import subprocess
import sys
from pathlib import Path

import numpy as np
from sklearn.ensemble import HistGradientBoostingRegressor
from sklearn.inspection import permutation_importance
from sklearn.linear_model import RidgeCV
from sklearn.model_selection import KFold
from sklearn.pipeline import make_pipeline
from sklearn.preprocessing import StandardScaler

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
from pool import world_type  # noqa: E402

LOGS = {"star_mass", "age_gyr", "orbit_au", "mass", "volatiles", "day_hours"}

# (name, regex on the card; first group = number), flags = substring present
CARD_NUM = [
    ("insolation", r"insolation ([\d.e+-]+)"), ("teff", r"Teff (\d+)"), ("xuv", r"XUV fluence ([\d.e+-]+)"),
    ("radius_km", r"R (\d+) km"), ("density", r"rho (\d+)"), ("gravity", r"g ([\d.]+) m/s2"),
    ("rotation_h", r"rotation\s+([\d.e+-]+) h"), ("heat_flow", r"heat flow ([\d.e+-]+)"),
    ("volcanism", r"volcanism ([\d.e+-]+) x"), ("surface_age", r"surface age ([\d.e+-]+)"),
    ("craters", r"craters ([\d.e+-]+) x"), ("relief_km", r"relief up to ~([\d.]+) km"),
    ("escape", r"escape\s+([\d.e+-]+) x"), ("pressure", r"atmosphere ([\d.e+-]+) bar"),
    ("cloud_opacity", r"mean opacity ([\d.]+)"), ("albedo", r"albedo ([\d.]+)"),
    ("t_eq", r"T_eq (\d+)"), ("t_mean", r"-> T (\d+) K"), ("t_winter_pole", r"winter pole (\d+)"),
    ("ice_cover", r"ice cover (\d+)%"), ("molten", r"(\d+)% still molten"),
]
CARD_FLAG = ["TIDALLY LOCKED", "host planet", "magma ocean", "young crust", "glowing lava", "deck", "haze",
             "liquids    water", "frozen over", "vegetation", "life       yes", "rust", "hematite", "goethite",
             "water-ice bedrock", "N2 ice", "tholin", "salts", "brine", "dark lag", "space-weathered",
             "Io-like", "sulfuric", "plate tectonics", "stagnant lid", "geologically dead", "clouds: [water",
             "field: none"]
LOG_CARD = {"insolation", "xuv", "heat_flow", "volcanism", "surface_age", "craters", "escape", "pressure", "rotation_h"}


def describe(seed, cache):
    if str(seed) not in cache:
        cache[str(seed)] = subprocess.run([str(ROOT / "build/earthgen"), "--random", str(seed), "--describe"],
                                          stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True).stdout
    return cache[str(seed)]


def slog(x):  # log scale that keeps 0 (and the -1 "never" of impact_myr) finite
    return math.log10(max(x, 0.0) + 1e-9)


def input_features(card):
    line = next(l for l in card.splitlines() if l.startswith("inputs"))
    kv = dict(t.split("=") for t in line.split()[1:])
    f = {}
    for k, v in kv.items():
        v = float(v)
        if k in LOGS:
            f["log_" + k] = math.log10(v)
        elif k in ("moon_mass_ratio", "host_mass_kg", "host_km"):
            f["log_" + k] = slog(v)
        elif k == "impact_myr":
            f["impact"] = float(v >= 0)
            f["log_impact_myr"] = math.log10(v) if v > 0 else 2.0  # never hit = long ago
        elif k == "composition":
            for c in range(3):
                f[f"composition_{c}"] = float(v == c)
        else:
            f[k] = v
    return f


def card_features(card):
    f = {}
    for name, rx in CARD_NUM:
        m = re.search(rx, card)
        v = float(m.group(1)) if m else 0.0
        f[name] = slog(v) if name in LOG_CARD else v
    for flag in CARD_FLAG:
        f["has " + flag.strip()] = float(flag in card)
    return f


def matrix(rows):
    keys = sorted({k for r in rows for k in r})
    return np.array([[r.get(k, 0.0) for k in keys] for r in rows]), keys


def pairwise_acc(pred, y, mask=None):
    """Share of pairs with different y ordered the same way by pred (ties in pred = half)."""
    i, j = np.triu_indices(len(y), 1)
    if mask is not None:
        keep = mask[i] & mask[j]
        i, j = i[keep], j[keep]
    d = np.sign(y[i] - y[j])
    p = np.sign(pred[i] - pred[j])
    use = d != 0
    return float(np.mean(np.where(p[use] == 0, 0.5, p[use] == d[use])))


def tree():
    return HistGradientBoostingRegressor(max_depth=3, learning_rate=0.05, max_iter=200, min_samples_leaf=15,
                                         l2_regularization=1.0, random_state=0)


def ridge():
    return make_pipeline(StandardScaler(), RidgeCV(alphas=np.logspace(-2, 4, 25)))


def oof(make, X, y, seed):
    pred = np.zeros(len(y))
    for tr, te in KFold(5, shuffle=True, random_state=seed).split(X):
        pred[te] = make().fit(X[tr], y[tr]).predict(X[te])
    return pred


def type_oof(types, y, seed):  # mean tier of the type in the training folds
    pred = np.zeros(len(y))
    types = np.array(types)
    for tr, te in KFold(5, shuffle=True, random_state=seed).split(y):
        means = {t: y[tr][types[tr] == t].mean() for t in set(types[tr])}
        pred[te] = [means.get(t, y[tr].mean()) for t in types[te]]
    return pred


def main():
    pool = json.loads((ROOT / "pool/index.json").read_text())
    log = [json.loads(l) for l in (ROOT / "pool/comparisons.jsonl").read_text().splitlines() if l.strip()]
    tiered = [j for j in log if "tiers" in j]
    latest = tiered[-1]
    tier_of = {s: 4 - i for i, t in enumerate(latest["tiers"]) for s in t}
    seeds = sorted(s for s in tier_of if str(s) in pool)
    y = np.array([tier_of[s] for s in seeds], float)
    out = []

    def say(msg=""):
        print(msg, flush=True)
        out.append(msg)

    say(f"{len(seeds)} planets from the judgment of {latest['t']} (tiers S..D: "
        + " ".join(str(int((y == 4 - i).sum())) for i in range(5)) + ")")

    # Noise ceiling: the user's own earlier tiering of the same planets.
    earlier = {}
    for j in tiered[:-1]:
        for i, t in enumerate(j["tiers"]):
            for s in t:
                earlier[s] = 4 - i
    twice = np.array([s in earlier for s in seeds])
    if twice.sum() > 10:
        prev = np.array([earlier.get(s, 0) for s in seeds], float)
        say(f"noise ceiling: earlier tiering of {twice.sum()} of these planets agrees on "
            f"{pairwise_acc(prev, y, twice):.3f} of pairs (0.5 = chance)")

    cache_f = ROOT / "models/cards.json"
    cache = json.loads(cache_f.read_text()) if cache_f.exists() else {}
    cards = [describe(s, cache) for s in seeds]
    cache_f.parent.mkdir(exist_ok=True)
    cache_f.write_text(json.dumps(cache))
    types = [pool[str(s)].get("type") or world_type(c) for s, c in zip(seeds, cards)]

    sets = {"inputs": matrix([input_features(c) for c in cards]),
            "card": matrix([card_features(c) for c in cards])}
    tx, tk = matrix([{"type " + t: 1.0} for t in types])
    sets["card + type"] = (np.hstack([sets["card"][0], tx]), sets["card"][1] + tk)
    for name in ("frozen", "ft"):
        f = ROOT / f"models/embeddings_vits14_{name}.npz"
        if f.exists():
            z = np.load(f)
            row = {int(s): i for i, s in enumerate(z["seed"])}
            e = z["emb"].mean(1)
            e /= np.linalg.norm(e, axis=1, keepdims=True)
            if all(s in row for s in seeds):
                sets[f"dino {name}"] = (e[[row[s] for s in seeds]], None)

    reps = range(5)
    results, preds = {}, {}

    def record(name, ps):
        accs = [pairwise_acc(p, y) for p in ps]
        results[name] = (np.mean(accs), np.std(accs))
        preds[name] = ps
        on191 = f"   (on the twice-tiered: {np.mean([pairwise_acc(p, y, twice) for p in ps]):.3f})" if twice.sum() > 10 else ""
        say(f"{name:28s} {np.mean(accs):.3f} +- {np.std(accs):.3f}{on191}")

    say("\npairwise accuracy, out-of-fold (mean +- sd over 5 repeats of 5-fold CV):")
    record("world type (mean tier)", [type_oof(types, y, r) for r in reps])
    best_tab, best_emb = None, None
    for name, (X, keys) in sets.items():
        if keys is None:
            record(name + " / ridge", [oof(ridge, X, y, r) for r in reps])
            if best_emb is None or results[name + " / ridge"][0] > results[best_emb][0]:
                best_emb = name + " / ridge"
            continue
        for mname, make in (("trees", tree), ("ridge", ridge)):
            record(f"{name} / {mname}", [oof(make, X, y, r) for r in reps])
            if best_tab is None or results[f"{name} / {mname}"][0] > results[best_tab][0]:
                best_tab = f"{name} / {mname}"
    if best_tab and best_emb:
        record(f"ensemble: {best_tab} + {best_emb.split(' /')[0]}",
               [(a - a.mean()) / a.std() + (b - b.mean()) / b.std() for a, b in zip(preds[best_tab], preds[best_emb])])

    # What drives it: permutation importance of the best tabular model, on held-out folds.
    name, mname = best_tab.split(" / ")
    X, keys = sets[name]
    imp = np.zeros(X.shape[1])
    for tr, te in KFold(5, shuffle=True, random_state=0).split(X):
        m = (tree if mname == "trees" else ridge)().fit(X[tr], y[tr])
        imp += permutation_importance(m, X[te], y[te], n_repeats=10, random_state=0).importances_mean / 5
    say(f"\nmost important features of {best_tab} (permutation importance, drop in R^2 on held-out folds):")
    for k in np.argsort(-imp)[:12]:
        hi = X[:, k] > np.median(X[:, k])
        direction = "" if hi.all() or (~hi).all() else f"   mean tier {y[hi].mean():.2f} above median vs {y[~hi].mean():.2f} below"
        say(f"  {imp[k]:+.3f}  {keys[k]}{direction}")

    say("\nmean tier per world type (S=4 .. D=0):")
    tt = {}
    for t, v in zip(types, y):
        tt.setdefault(t, []).append(v)
    for t, v in sorted(tt.items(), key=lambda kv: -np.mean(kv[1])):
        say(f"  {np.mean(v):.2f}  (n={len(v):2d})  {t}")
    (ROOT / "models/prefs_results.txt").write_text("\n".join(out) + "\n")


if __name__ == "__main__":
    main()
