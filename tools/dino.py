"""Fine-tune DINOv2 on rendered planets (views/ from tools/views.py), DINO-style:
student/teacher with an EMA teacher, multi-crop, centering + sharpening (Caron 2021).
Only the last TRAIN_BLOCKS blocks train; the frozen part runs once per crop under
no_grad and is shared by student and teacher (identical weights there). Views of the
same planet (other yaw, sun, weather) are the two global crops: true positives.
Teacher centering by Sinkhorn-Knopp as in DINOv2 (--center ema = DINO's running mean: with
batches of 16 it collapsed onto ~10 slots). No iBOT / KoLeo: DINO loss on the CLS token only.

    uv run ... python tools/dino.py train  [--arch vits14] [--epochs 30] [--batch 16]
    uv run ... python tools/dino.py sheet  [--n 12]       # nearest neighbours: frozen vs fine-tuned

Eval (held-out seeds, 15%; CLS of the teacher backbone, cosine):
  view@1  each view's nearest other image is a view of the same planet (all 4 views)
  hard@1  view 0 queried against view 3 of every held-out planet (most different views)
  type@5  5-NN majority vote of the world type (from the physics card) vs train planets
Outputs in models/: dino_<arch>.pt (trainable blocks + norm), embeddings_<arch>_{frozen,ft}.npz
(seed, type, emb [N, 4 views, D]), train log in models/dino_<arch>.log.
"""
import argparse
import copy
import json
import math
import random
import time
from pathlib import Path

import numpy as np
import torch
import torch.nn as nn
import torch.nn.functional as F
from PIL import Image
from torchvision.transforms import v2 as T

ROOT = Path(__file__).resolve().parent.parent
MEAN, STD = (0.485, 0.456, 0.406), (0.229, 0.224, 0.225)  # ImageNet, as DINOv2 was trained


def load_views(views_dir):
    index = json.loads((views_dir / "index.json").read_text())
    seeds = sorted(int(s) for s, e in index.items() if len(e["views"]) == 4)
    imgs = torch.empty(len(seeds), 4, 3, 224, 224, dtype=torch.uint8)
    for i, s in enumerate(seeds):
        for v, name in enumerate(index[str(s)]["views"]):
            imgs[i, v] = torch.from_numpy(np.asarray(Image.open(views_dir / str(s) / name).convert("RGB"))).permute(2, 0, 1)
    return seeds, [index[str(s)]["type"] for s in seeds], imgs


def held_out(seed):  # fixed 15 % split by seed
    return (seed * 2654435761) % 100 < 15


class Backbone:
    """DINOv2 ViT split into a frozen bottom and a trainable top."""

    def __init__(self, arch, train_blocks, device):
        self.vit = torch.hub.load("facebookresearch/dinov2", f"dinov2_{arch}").to(device).eval()
        self.split = len(self.vit.blocks) - train_blocks
        for p in self.vit.parameters():
            p.requires_grad = False
        self.dim = self.vit.embed_dim

    @torch.no_grad()
    def bottom(self, x):
        x = self.vit.prepare_tokens_with_masks(x, None)
        for blk in self.vit.blocks[:self.split]:
            x = blk(x)
        return x

    def top_modules(self):
        return nn.ModuleList([*self.vit.blocks[self.split:], self.vit.norm])


def run_top(top, x):
    for blk in top[:-1]:
        x = blk(x)
    return top[-1](x)[:, 0]  # normed CLS


class Head(nn.Module):  # DINO projection head
    def __init__(self, dim, k=1024, hidden=2048, bottleneck=256):
        super().__init__()
        self.mlp = nn.Sequential(nn.Linear(dim, hidden), nn.GELU(), nn.Linear(hidden, hidden), nn.GELU(),
                                 nn.Linear(hidden, bottleneck))
        self.last = nn.utils.parametrizations.weight_norm(nn.Linear(bottleneck, k, bias=False))
        self.last.parametrizations.weight.original0.data.fill_(1)
        self.last.parametrizations.weight.original0.requires_grad = False

    def forward(self, x):
        return self.last(F.normalize(self.mlp(x), dim=-1))


@torch.no_grad()
def sinkhorn(logits, iters=3):
    q = torch.exp(logits - logits.max()).T  # K x B
    q /= q.sum()
    k, b = q.shape
    for _ in range(iters):
        q /= q.sum(1, keepdim=True) * k  # each slot gets 1/K of the mass
        q /= q.sum(0, keepdim=True) * b  # each sample is a distribution
    return (q * b).T


def augmenters():
    norm = T.Compose([T.ToDtype(torch.float32, scale=True), T.Normalize(MEAN, STD)])
    # No hue/saturation jitter: colour is a big part of what makes worlds different.
    photo = [T.RandomHorizontalFlip(), T.RandomApply([T.ColorJitter(0.2, 0.2)], p=0.5),
             T.RandomApply([T.GaussianBlur(9, (0.1, 1.5))], p=0.2)]
    glob = T.Compose([T.RandomResizedCrop(224, scale=(0.4, 1.0), antialias=True), *photo, norm])
    loc = T.Compose([T.RandomResizedCrop(98, scale=(0.05, 0.4), antialias=True), *photo, norm])
    return glob, loc, norm


@torch.no_grad()
def embed(bb, top, imgs, device, norm, batch=64):
    out = []
    flat = imgs.flatten(0, 1)
    for i in range(0, len(flat), batch):
        x = norm(flat[i:i + batch]).to(device)
        out.append(F.normalize(run_top(top, bb.bottom(x)), dim=-1).float().cpu())
    return torch.cat(out).view(imgs.shape[0], 4, -1)


def evaluate(emb, types, test, train):
    e = emb[test]
    n = len(test)
    flat = e.flatten(0, 1)
    sim = flat @ flat.T
    sim.fill_diagonal_(-2)
    view1 = ((sim.argmax(1) // 4) == torch.arange(n).repeat_interleave(4)).float().mean().item()
    hard1 = ((e[:, 0] @ e[:, 3].T).argmax(1) == torch.arange(n)).float().mean().item()
    gal = emb[train].mean(1)
    nn5 = (e.mean(1) @ gal.T).topk(5, dim=1).indices
    tr_types = [types[i] for i in train]
    ok = 0
    for q, row in zip(test, nn5):
        votes = [tr_types[j] for j in row.tolist()]
        ok += max(set(votes), key=votes.count) == types[q]
    return {"view@1": view1, "hard@1": hard1, "type@5": ok / n}


def save_embeddings(path, seeds, types, emb):
    np.savez(path, seed=np.array(seeds), type=np.array(types), emb=emb.numpy())


def train(a):
    device = "cuda" if torch.cuda.is_available() else "cpu"
    torch.manual_seed(0)
    random.seed(0)
    out = ROOT / "models"
    out.mkdir(exist_ok=True)
    log = open(out / f"dino_{a.arch}.log", "a")

    def say(msg):
        print(msg, flush=True)
        log.write(msg + "\n")
        log.flush()

    seeds, types, imgs = load_views(ROOT / a.views)
    test = [i for i, s in enumerate(seeds) if held_out(s)]
    tr = [i for i, s in enumerate(seeds) if not held_out(s)]
    say(f"{time.ctime()}: {a.arch}, {len(seeds)} planets ({len(tr)} train, {len(test)} held out), device {device}")
    bb = Backbone(a.arch, a.train_blocks, device)
    glob, loc, norm = augmenters()

    student = bb.top_modules().to(device)
    teacher = copy.deepcopy(student)
    for p in teacher.parameters():
        p.requires_grad = False
    for p in student.parameters():
        p.requires_grad = True
    s_head, t_head = Head(bb.dim, a.k).to(device), Head(bb.dim, a.k).to(device)
    t_head.load_state_dict(s_head.state_dict())
    for p in t_head.parameters():
        p.requires_grad = False

    emb0 = embed(bb, teacher, imgs, device, norm)
    save_embeddings(out / f"embeddings_{a.arch}_frozen.npz", seeds, types, emb0)
    best = evaluate(emb0, types, test, tr)
    say(f"frozen: " + "  ".join(f"{k} {v:.3f}" for k, v in best.items()))

    opt = torch.optim.AdamW([{"params": student.parameters(), "lr": a.lr},
                             {"params": [p for p in s_head.parameters() if p.requires_grad], "lr": a.lr * a.head_lr}],
                            weight_decay=0.04)
    base_lr = [g["lr"] for g in opt.param_groups]
    steps_per_epoch = len(tr) // a.batch
    total = steps_per_epoch * a.epochs
    center = torch.zeros(1, a.k, device=device)
    step = 0
    for epoch in range(a.epochs):
        random.shuffle(tr)
        t_ep, losses, ent = time.time(), [], []
        for b in range(steps_per_epoch):
            idx = tr[b * a.batch:(b + 1) * a.batch]
            # two different views of each planet as the global crops, locals from any view
            pairs = [random.sample(range(4), 2) for _ in idx]
            g = [torch.stack([glob(imgs[i, p[j]]) for i, p in zip(idx, pairs)]).to(device) for j in range(2)]
            l = torch.stack([loc(imgs[i, random.randrange(4)]) for i in idx for _ in range(a.locals)]).to(device)
            l = l.view(len(idx), a.locals, 3, 98, 98).transpose(0, 1).flatten(0, 1)  # crop-major
            frac = step / total
            for grp, lr in zip(opt.param_groups, base_lr):
                warm = min(1.0, step / steps_per_epoch)
                grp["lr"] = lr * warm * 0.5 * (1 + math.cos(math.pi * frac))
            mom = 1 - (1 - a.momentum) * 0.5 * (1 + math.cos(math.pi * frac))

            gb = [bb.bottom(x) for x in g]
            lb = bb.bottom(l)
            with torch.no_grad():
                t_out = [t_head(run_top(teacher, x)) for x in gb]
                if a.center == "sk":  # DINOv2: Sinkhorn-Knopp, equal slot use within the batch
                    t_prob = list(sinkhorn(torch.cat(t_out) / a.t_temp).chunk(2))
                else:
                    t_prob = [F.softmax((t - center) / a.t_temp, dim=-1) for t in t_out]
            s_out = [s_head(run_top(student, x)) for x in gb] + list(s_head(run_top(student, lb)).chunk(a.locals))
            loss, n = 0.0, 0
            for ti, tp in enumerate(t_prob):
                for si, so in enumerate(s_out):
                    if si == ti:
                        continue
                    loss = loss + torch.sum(-tp * F.log_softmax(so / 0.1, dim=-1), dim=-1).mean()
                    n += 1
            loss = loss / n
            opt.zero_grad(set_to_none=True)
            loss.backward()
            nn.utils.clip_grad_norm_([*student.parameters(), *s_head.parameters()], 3.0)
            if epoch == 0:  # DINO: keep the last layer of the head fixed for the first epoch
                s_head.last.parametrizations.weight.original1.grad = None
            opt.step()
            with torch.no_grad():
                for ps, pt in zip([*student.parameters(), *s_head.parameters()], [*teacher.parameters(), *t_head.parameters()]):
                    pt.mul_(mom).add_(ps.detach(), alpha=1 - mom)
                batch_center = torch.cat(t_out).mean(0, keepdim=True)
                center = center * 0.9 + batch_center * 0.1
                p = torch.cat(t_prob).mean(0)
                ent.append(-(p * (p + 1e-12).log()).sum().item())  # entropy of the mean teacher output
            losses.append(loss.item())
            step += 1
        msg = f"epoch {epoch + 1}/{a.epochs}  loss {np.mean(losses):.3f}  slots used e^H {math.exp(np.mean(ent)):.0f}  {time.time() - t_ep:.0f}s"
        if (epoch + 1) % a.eval_every == 0 or epoch + 1 == a.epochs:
            emb = embed(bb, teacher, imgs, device, norm)
            m = evaluate(emb, types, test, tr)
            msg += "  |  " + "  ".join(f"{k} {v:.3f}" for k, v in m.items())
            torch.save({"arch": a.arch, "train_blocks": a.train_blocks, "top": teacher.state_dict()},
                       out / f"dino_{a.arch}.pt")
            save_embeddings(out / f"embeddings_{a.arch}_ft.npz", seeds, types, emb)
        say(msg)
    say(f"{time.ctime()}: done")


def sheet(a):
    """Query planets (held out) with their nearest neighbours, frozen row above fine-tuned row."""
    index = json.loads((ROOT / a.views / "index.json").read_text())
    fz = np.load(ROOT / "models" / f"embeddings_{a.arch}_frozen.npz")
    ft = np.load(ROOT / "models" / f"embeddings_{a.arch}_ft.npz")
    seeds = fz["seed"].tolist()
    rng = random.Random(a.seed)
    queries = rng.sample([i for i, s in enumerate(seeds) if held_out(s)], a.n)
    tile, k = 112, 6
    img = Image.new("RGB", (tile * (k + 1) + 8, tile * 2 * a.n + 4 * a.n), (40, 40, 40))
    for r, q in enumerate(queries):
        for row, z in enumerate((fz, ft)):
            e = z["emb"].mean(1)
            e = e / np.linalg.norm(e, axis=1, keepdims=True)
            sim = e @ e[q]
            sim[q] = -2
            nn_ = [q] + np.argsort(-sim)[:k].tolist()
            y = r * (2 * tile + 4) + row * tile
            for c, j in enumerate(nn_):
                im = Image.open(ROOT / a.views / str(seeds[j]) / index[str(seeds[j])]["views"][0]).convert("RGB")
                img.paste(im.resize((tile, tile), Image.LANCZOS), (c * tile + (8 if c else 0), y))
    out = ROOT / "renders" / "dino" / f"neighbors_{a.arch}.png"
    out.parent.mkdir(parents=True, exist_ok=True)
    img.save(out)
    print(f"wrote {out}: each pair of rows = query (left) + 6 nearest; upper row frozen, lower fine-tuned")


def main():
    p = argparse.ArgumentParser()
    p.add_argument("cmd", choices=["train", "sheet"])
    p.add_argument("--arch", default="vits14")
    p.add_argument("--views", default="views")
    p.add_argument("--epochs", type=int, default=30)
    p.add_argument("--batch", type=int, default=16)
    p.add_argument("--locals", type=int, default=6)
    p.add_argument("--train-blocks", type=int, default=4)
    p.add_argument("--k", type=int, default=1024)
    p.add_argument("--lr", type=float, default=1e-4)
    p.add_argument("--head-lr", type=float, default=20, help="head lr = lr x this")
    p.add_argument("--center", choices=["sk", "ema"], default="sk", help="teacher centering: Sinkhorn-Knopp or EMA mean")
    p.add_argument("--t-temp", type=float, default=0.04)
    p.add_argument("--momentum", type=float, default=0.99)
    p.add_argument("--eval-every", type=int, default=2)
    p.add_argument("--n", type=int, default=12)
    p.add_argument("--seed", type=int, default=0)
    a = p.parse_args()
    train(a) if a.cmd == "train" else sheet(a)


if __name__ == "__main__":
    main()
