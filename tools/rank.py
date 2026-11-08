"""Ranking app for the planet pool (tools/pool.py), two ways to judge:
- Tiers (main page): a batch of ~40 planets (the least-shown ones) and 5 tiers S-D; drag
  planets into tiers, or hover one and press 1-5 (0 = back to unsorted). Hovering shows
  a large preview. The unsorted planets come one at a time (click a tier or 1-5 places it, space = later).
  Unsorted planets are left out. "+10 planets" adds the next least-shown
  ones to the batch in progress. "send" logs the batch; "undo" takes the
  last sent batch back for editing. The batch in progress survives a page reload.
- Rank (/rank): a set of 2-6 planets clicked in order of preference (1st, 2nd, ...; click
  a ranked one again to unrank it); sent once all but one are ranked, Enter sends a
  partial ranking. Keys: 1-6, Backspace = unrank last, s = skip, u = undo, c = cards.
Every judgment is appended to POOL/comparisons.jsonl:
    {"t": time, "shown": [seeds], "tiers": [[S seeds], [A], [B], [C], [D]]}
    {"t": time, "shown": [seeds], "order": [seeds ranked, best first]}   ([] = skipped)
(older lines have "best": seed instead, = an order of one).
The ranking page fits a Bradley-Terry model to the pairs these imply (a planet beats
every planet in a lower tier / ranked after it / left unranked below a ranking; same
tier = no information; a weak prior of one win + one loss against an average planet
keeps rarely seen planets near 0). Rank sets: a least-shown planet + the planets closest
to it in score from a random sample (a Swiss tournament).
    uv run --no-project python tools/rank.py [POOL] [PORT]   # default pool/, http://localhost:8766
"""
import json
import math
import random
import sys
import time
import webbrowser
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import parse_qs, urlparse

POOL = Path(sys.argv[1] if len(sys.argv) > 1 else Path(__file__).resolve().parent.parent / "pool")
PORT = int(sys.argv[2]) if len(sys.argv) > 2 else 8766
LOG = POOL / "comparisons.jsonl"
SAMPLE = 24  # candidates the opponents are drawn from (closest in score)

STYLE = """<style>
body{background:#0c0c10;color:#e6e6eb;font:14px system-ui,sans-serif;margin:16px}
a{color:#7fb8ff} .hint{color:#8a8a96} .top{display:flex;gap:18px;align-items:center;flex-wrap:wrap}
.row{display:flex;flex-wrap:wrap;gap:14px;margin-top:14px}
button{background:#16161c;color:#e6e6eb;border:1px solid #444;border-radius:4px;padding:4px 10px;cursor:pointer}
.tile{cursor:pointer;border:3px solid transparent;border-radius:6px;padding:6px;background:#16161c}
.tile:hover{border-color:#4cc38a} .tile.on{border-color:#4cc38a;background:#13261d}
.rank{color:#4cc38a;font-size:18px;margin-left:8px}
.tile img{display:block;image-rendering:pixelated}
.id{font-weight:600;margin-top:4px} .card{color:#9a9aa6;font-size:12px;max-width:380px;word-break:break-word}
.small .tile{cursor:default;padding:4px} .small .tile:hover{border-color:transparent}
</style>"""

PAGE = """<!doctype html><html><head><meta charset="utf-8"><title>Planet ranking</title>""" + STYLE + """</head><body>
<div class="top"><h2 style="margin:0">Rank them, best first</h2>
<label>planets per set <select id="n"><option>2</option><option>3</option><option>4</option><option>6</option></select></label>
<button id="send">send (Enter)</button>
<span id="count" class="hint"></span><a href="/">tiers</a><a href="/ranking">ranking</a></div>
<p class="hint">click (or 1-6) in order of preference; sent when all but one are ranked, Enter sends a partial ranking.
Backspace = unrank last, s = skip, u = undo last sent set, c = physics cards</p>
<div id="set" class="row"></div>
<script>
let shown = [], order = [], tiles = [], cards = false, busy = false;
const nsel = document.getElementById('n');
try { nsel.value = localStorage.getItem('n') || '6'; } catch(e) {}
nsel.onchange = () => { try { localStorage.setItem('n', nsel.value); } catch(e) {} next(); };
document.getElementById('send').onclick = () => send();
function draw(){
  tiles.forEach((d, i) => {
    const r = order.indexOf(shown[i]);
    d.classList.toggle('on', r >= 0);
    d.querySelector('.rank').textContent = r >= 0 ? '#' + (r + 1) : '';
  });
}
function click(i){
  if (busy) return;
  const s = shown[i], r = order.indexOf(s);
  if (r >= 0) order.splice(r, 1); else order.push(s);
  draw();
  if (order.length >= shown.length - 1) send();
}
async function next(){
  const r = await fetch('/next?n=' + nsel.value).then(r => r.json());
  shown = r.set.map(p => p.seed); order = [];
  document.getElementById('count').textContent = r.judged + ' sets judged, ' + r.pool + ' planets';
  const px = shown.length > 4 ? 256 : shown.length > 2 ? 300 : 384;
  const root = document.getElementById('set'); root.innerHTML = '';
  root.style.maxWidth = shown.length > 4 ? (3 * (px + 34)) + 'px' : '';
  tiles = r.set.map((p, i) => {
    const d = document.createElement('div'); d.className = 'tile';
    d.innerHTML = `<img src="/img/${p.file}" width="${px}" height="${px}"><div class="id">${i+1} <span class="rank"></span></div>`
      + `<div class="card" style="display:${cards?'block':'none'}">#${p.seed}: ${p.card}</div>`;
    d.onclick = () => click(i);
    root.appendChild(d);
    return d;
  });
  if (!r.set.length) root.textContent = 'Pool has fewer than 2 planets: run tools/pool.py first.';
  busy = false;
}
async function send(){
  if (busy) return; busy = true;
  if (order.length === shown.length - 1) order.push(shown.find(s => !order.includes(s)));
  await fetch('/judge', {method:'POST', body: JSON.stringify({shown, order})});
  next();
}
document.onkeydown = e => {
  if (e.target.tagName === 'SELECT') return;
  const k = e.key;
  if (k >= '1' && k <= String(shown.length)) click(+k - 1);
  else if (k === 'Enter') { e.preventDefault(); if (order.length) send(); }
  else if (k === 'Backspace') { order.pop(); draw(); }
  else if (k === 's') { order = []; send(); }
  else if (k === 'u') { busy = true; fetch('/undo', {method:'POST'}).then(next); }
  else if (k === 'c') { cards = !cards; document.querySelectorAll('.card').forEach(c => c.style.display = cards ? 'block' : 'none'); }
};
next();
</script></body></html>"""


TIERS = """<!doctype html><html><head><meta charset="utf-8"><title>Planet tiers</title>""" + STYLE + """
<style>
.tier{display:flex;gap:0;margin-top:8px;background:#121218;border-radius:6px;cursor:pointer}
.tier:hover{outline:2px solid #4cc38a55}
.tier .lab{width:64px;flex:none;display:flex;flex-direction:column;align-items:center;justify-content:center;font-size:26px;font-weight:700;border-radius:6px 0 0 6px;color:#111}
.lab small{font-size:12px;font-weight:400;text-align:center;line-height:1.2} .lab button{font-size:11px;padding:1px 5px;margin-top:3px}
.zone{display:flex;flex-wrap:wrap;gap:6px;padding:6px;flex:1;align-content:flex-start;min-height:100px}
.zone.collapsed{max-height:100px;overflow:hidden}
.zone.over{background:#1b2a22}
.pl{cursor:grab;border:2px solid transparent;border-radius:4px;line-height:0}
.pl:hover{border-color:#4cc38a} .pl img{image-rendering:pixelated}
.main{display:grid;grid-template-columns:minmax(0,1fr) 392px;gap:12px}
.col{height:calc(100vh - 100px);overflow-y:auto}
#pool{margin-top:8px;background:#101014;border:1px dashed #333;border-radius:6px;min-height:372px;justify-content:center}
#pool .pl{display:none} #pool .pl:first-child{display:block} #pool .pl:first-child img{width:360px;height:360px}
#preview{position:fixed;bottom:12px;width:384px;pointer-events:none;background:#16161cee;border-radius:6px;padding:6px;display:none}
#preview img{width:384px;height:384px;image-rendering:pixelated;display:block}
</style></head><body>
<div class="top"><h2 style="margin:0">Tiers</h2>
<label>batch <select id="n"><option>20</option><option>40</option><option>60</option><option>100</option></select></label>
<button id="send">send batch</button><button id="more">+10 planets</button><button id="undo">undo last sent</button><button id="fresh">new batch</button>
<span id="count" class="hint"></span><a href="/rank">rank sets</a><a href="/ranking">ranking</a></div>
<p class="hint">Click a tier (or 1-5) to put the next unsorted planet (right) there, space = later; or drag it. Hover a placed planet for a
large preview and press 1-5 / 0 (back to unsorted) to move it; c = physics cards. Unsorted planets are left out of the batch. A tier shows its newest planets in one row; "all" expands it.</p>
<div class="main"><div id="tiers" class="col"></div><div class="col"><div id="left" class="hint"></div><div id="pool" class="zone"></div><div id="poolcard" class="card"></div></div></div>
<div id="preview"><img><div class="card"></div></div>
<script>
const NAMES = ['S','A','B','C','D'], COLORS = ['#ff7f7f','#ffbf7f','#ffdf7f','#bfff7f','#7fbfff'];
let batch = [], info = {}, hover = null, cards = false;
const nsel = document.getElementById('n'), pool = document.getElementById('pool'), zones = [];
try { nsel.value = localStorage.getItem('tier_n') || '40'; } catch(e) {}
nsel.onchange = () => { try { localStorage.setItem('tier_n', nsel.value); } catch(e) {} };
const tiersDiv = document.getElementById('tiers');
NAMES.forEach((name, i) => {
  const row = document.createElement('div'); row.className = 'tier';
  row.innerHTML = `<div class="lab" style="background:${COLORS[i]}">${name}<small></small><button>all</button></div>`;
  const z = document.createElement('div'); z.className = 'zone collapsed'; row.appendChild(z);
  row.querySelector('button').onclick = e => { e.stopPropagation(); z.classList.toggle('collapsed'); counts(); };
  row.onclick = () => { if (pool.firstElementChild) place(+pool.firstElementChild.dataset.seed, i); };  // the next unsorted one
  tiersDiv.appendChild(row); zones.push(z);
});
[...zones, pool].forEach(z => {
  z.ondragover = e => { e.preventDefault(); z.classList.add('over'); };
  z.ondragleave = () => z.classList.remove('over');
  z.ondrop = e => { e.preventDefault(); z.classList.remove('over');
    const el = document.getElementById('p' + e.dataTransfer.getData('text'));
    if (el) { if (z === pool) z.appendChild(el); else z.prepend(el); save(); } };
});
// Newest first in a tier, so a collapsed tier (one row) shows the latest placements.
function place(seed, tier, front = true){
  const el = document.getElementById('p' + seed);
  if (tier < 0) pool.appendChild(el); else if (front) zones[tier].prepend(el); else zones[tier].appendChild(el);
  save();
}
function counts(){  // per tier: total, and how many the collapsed row hides
  zones.forEach(z => {
    const lab = z.parentNode.querySelector('.lab'), kids = [...z.children];
    const top = kids.length ? kids[0].offsetTop : 0;
    const hidden = z.classList.contains('collapsed') ? kids.filter(c => c.offsetTop > top + 10).length : 0;
    lab.querySelector('small').innerHTML = kids.length ? kids.length + (hidden ? `<br>+${hidden} more` : '') : '';
    const b = lab.querySelector('button');
    b.textContent = z.classList.contains('collapsed') ? 'all' : 'less';
    b.style.visibility = hidden || !z.classList.contains('collapsed') ? 'visible' : 'hidden';
  });
}
addEventListener('resize', counts);
function state(){ return {batch, tiers: zones.map(z => [...z.children].map(c => +c.dataset.seed))}; }
function save(){
  try { localStorage.setItem('tier_state', JSON.stringify(state())); } catch(e) {}
  const placed = state().tiers.flat().length;
  document.getElementById('count').textContent = `${placed} of ${batch.length} placed; ${info.judged} judgments, ${info.pool} planets`;
  const n = pool.children.length, first = pool.firstElementChild;
  document.getElementById('left').textContent = n ? `next of ${n} unsorted` : 'all sorted: send the batch, or +10 planets';
  document.getElementById('poolcard').textContent = cards && first ? `#${first.dataset.seed}: ${info.byseed[first.dataset.seed].card}` : '';
  counts();
}
function preview(seed){
  const pv = document.getElementById('preview');
  if (seed === null || document.getElementById('p' + seed).parentNode === pool) { pv.style.display = 'none'; return; }
  const p = info.byseed[seed];
  pv.querySelector('img').src = '/img/' + p.file;
  pv.querySelector('.card').textContent = cards ? `#${seed}: ${p.card}` : '';
  const left = document.getElementById('p' + seed).getBoundingClientRect().left > innerWidth / 2;
  pv.style.left = left ? '12px' : ''; pv.style.right = left ? '' : '12px';  // on the other column
  pv.style.display = 'block';
}
function tile(s){
  const d = document.createElement('div'); d.className = 'pl'; d.id = 'p' + s; d.dataset.seed = s; d.draggable = true;
  d.innerHTML = `<img src="/img/${info.byseed[s].file}" width="96" height="96" draggable="false">`;
  d.ondragstart = e => { e.dataTransfer.setData('text', s); preview(null); };
  d.onmouseenter = () => { hover = s; preview(s); };
  d.onmouseleave = () => { hover = null; preview(null); };
  pool.appendChild(d);
}
function show(seeds, tiers){
  batch = seeds; pool.innerHTML = ''; zones.forEach(z => z.innerHTML = '');
  seeds.forEach(tile);
  (tiers || []).forEach((t, i) => t.forEach(s => { if (document.getElementById('p' + s)) place(s, i, false); }));
  save();
}
async function load(fresh){
  info = await fetch('/pool').then(r => r.json());
  let st = null;
  try { st = JSON.parse(localStorage.getItem('tier_state')); } catch(e) {}
  if (!fresh && st && st.batch.length && st.batch.every(s => s in info.byseed)) return show(st.batch, st.tiers);
  const r = await fetch('/batch?n=' + nsel.value).then(r => r.json());
  show(r.batch, null);
}
document.getElementById('send').onclick = async () => {
  const st = state();
  if (!st.tiers.flat().length) return;
  await fetch('/judge', {method:'POST', body: JSON.stringify({shown: st.batch, tiers: st.tiers})});
  try { localStorage.removeItem('tier_state'); } catch(e) {}
  load(true);
};
document.getElementById('fresh').onclick = () => load(true);
document.getElementById('more').onclick = async () => {  // more planets into the batch in progress
  info = await fetch('/pool').then(r => r.json());  // (the pool may have grown meanwhile)
  const r = await fetch('/batch?n=10&exclude=' + batch.join(',')).then(r => r.json());
  r.batch.forEach(s => { batch.push(s); tile(s); });
  save();
};
document.getElementById('undo').onclick = async () => {
  const last = await fetch('/undo', {method:'POST'}).then(r => r.json());
  info = await fetch('/pool').then(r => r.json());
  if (last && last.tiers) show(last.shown.filter(s => s in info.byseed), last.tiers); else save();
};
document.onkeydown = e => {
  if (e.target.tagName === 'SELECT') return;
  // A hovered placed planet, else the next unsorted one (the big one on the right).
  const h = hover !== null && document.getElementById('p' + hover).parentNode !== pool ? hover : null;
  const t = h !== null ? h : pool.firstElementChild ? +pool.firstElementChild.dataset.seed : null;
  if (e.key === 'c') { cards = !cards; preview(h); save(); return; }
  if (t === null) return;
  if (e.key >= '1' && e.key <= '5') place(t, +e.key - 1);
  else if (e.key === '0') place(t, -1);
  else if (e.key === ' ' && h === null) { e.preventDefault(); pool.appendChild(pool.firstElementChild); save(); }
};
load(false);
</script></body></html>"""


def load_pool():
    f = POOL / "index.json"
    return json.loads(f.read_text()) if f.exists() else {}


def load_log():
    if not LOG.exists():
        return []
    log = [json.loads(l) for l in LOG.read_text().splitlines() if l.strip()]
    for j in log:  # -> "groups": best first; each planet beats those in later groups
        if "tiers" in j:
            j["groups"] = j["tiers"]
            continue
        if "order" not in j:  # older lines: best of the set only
            j["order"] = [] if j.get("best") is None else [j["best"]]
        rest = [s for s in j["shown"] if s not in j["order"]]
        j["groups"] = [[s] for s in j["order"]] + ([rest] if j["order"] and rest else [])
    return log


def fit(seeds, log, iters=200):
    """Bradley-Terry strengths by Hunter's MM algorithm, prior = 1 win + 1 loss vs a
    fixed planet of strength 1. Each judgment's groups (tiers, or a ranking) give the
    pairs: a planet beats every planet in a later group. Returns {seed: log strength},
    {seed: times shown}."""
    wins = {s: 1.0 for s in seeds}          # prior win
    games = {s: {} for s in seeds}           # opponent -> number of comparisons
    shown = {s: 0 for s in seeds}
    for j in log:
        for s in j["shown"]:
            if s in shown:
                shown[s] += 1
        groups = [[s for s in g if s in wins] for g in j["groups"]]
        for i, g in enumerate(groups):
            below = [s for h in groups[i + 1:] for s in h]
            for b in g:
                for s in below:
                    wins[b] += 1
                    games[b][s] = games[b].get(s, 0) + 1
                    games[s][b] = games[s].get(b, 0) + 1
    p = {s: 1.0 for s in seeds}
    for _ in range(iters):
        q = {}
        for s in seeds:
            denom = 2.0 / (p[s] + 1.0)       # prior games vs the strength-1 planet
            for o, n in games[s].items():
                denom += n / (p[s] + p[o])
            q[s] = wins[s] / denom
        p = q
    return {s: math.log(p[s]) for s in seeds}, shown


def pick_set(n):
    pool = load_pool()
    seeds = [int(s) for s in pool]
    if len(seeds) < 2:
        return [], 0, len(seeds)
    log = load_log()
    score, shown = fit(seeds, log, iters=50)
    least = min(shown.values())
    first = random.choice([s for s in seeds if shown[s] == least])
    rest = random.sample([s for s in seeds if s != first], min(SAMPLE, len(seeds) - 1))
    rest.sort(key=lambda s: abs(score[s] - score[first]))
    chosen = [first] + rest[:n - 1]
    random.shuffle(chosen)
    return [{"seed": s, **pool[str(s)]} for s in chosen], len(log), len(seeds)


def pick_batch(n, exclude=()):
    seeds = [int(s) for s in load_pool() if int(s) not in exclude]
    _, shown = fit(seeds, load_log(), iters=0)
    random.shuffle(seeds)
    seeds.sort(key=lambda s: shown[s])  # least shown first (stable: random among equals)
    return seeds[:n]


def ranking_page():
    pool = load_pool()
    seeds = [int(s) for s in pool]
    log = load_log()
    score, shown = fit(seeds, log)
    hist = {s: [] for s in seeds}  # per planet: tier letters / rank positions
    for j in log:
        for i, g in enumerate(j["groups"]):
            for s in g:
                if s in hist:
                    hist[s].append("SABCD"[i] if "tiers" in j else
                                   f"{i + 1}/{len(j['shown'])}" if i < len(j["order"]) else "-")
    order = sorted(seeds, key=lambda s: -score[s])
    tiles = "".join(
        f'<div class="tile"><img src="/img/{pool[str(s)]["file"]}" width="192" height="192">'
        f'<div class="id">{i + 1}. #{s} &nbsp; {score[s]:+.2f}</div>'
        f'<div class="card">shown {shown[s]}: {" ".join(hist[s])}</div>'
        f'<div class="card">{pool[str(s)]["card"]}</div></div>'
        for i, s in enumerate(order))
    return ("""<!doctype html><html><head><meta charset="utf-8"><title>Planet ranking</title>""" + STYLE
            + f"""</head><body><div class="top"><h2 style="margin:0">Ranking</h2>
<span class="hint">{len(log)} sets judged, {len(seeds)} planets; score = Bradley-Terry log strength
(0 = average, +1 = wins ~73% against an average planet)</span><a href="/">tiers</a><a href="/rank">rank sets</a></div>
<div class="row small">{tiles}</div></body></html>""")


class Handler(BaseHTTPRequestHandler):
    def send(self, body, ctype, code=200):
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        url = urlparse(self.path)
        if url.path == "/":
            self.send(TIERS.encode(), "text/html; charset=utf-8")
        elif url.path == "/rank":
            self.send(PAGE.encode(), "text/html; charset=utf-8")
        elif url.path == "/pool":
            self.send(json.dumps({"byseed": load_pool(), "judged": len(load_log()), "pool": len(load_pool())}).encode(),
                      "application/json")
        elif url.path == "/batch":
            q = parse_qs(url.query)
            n = max(1, int(q.get("n", ["40"])[0]))
            exclude = {int(x) for x in q.get("exclude", [""])[0].split(",") if x}
            self.send(json.dumps({"batch": pick_batch(n, exclude)}).encode(), "application/json")
        elif url.path == "/ranking":
            self.send(ranking_page().encode(), "text/html; charset=utf-8")
        elif url.path == "/next":
            n = max(2, min(6, int(parse_qs(url.query).get("n", ["2"])[0])))
            chosen, judged, size = pick_set(n)
            self.send(json.dumps({"set": chosen, "judged": judged, "pool": size}).encode(), "application/json")
        elif url.path.startswith("/img/"):
            f = (POOL / url.path[5:]).resolve()
            if f.parent == POOL.resolve() and f.suffix == ".png" and f.exists():
                self.send(f.read_bytes(), "image/png")
            else:
                self.send(b"not found", "text/plain", 404)
        else:
            self.send(b"not found", "text/plain", 404)

    def do_POST(self):
        if self.path == "/judge":
            data = json.loads(self.rfile.read(int(self.headers["Content-Length"])))
            entry = {"t": time.strftime("%Y-%m-%dT%H:%M:%S"), "shown": data["shown"]}
            entry.update({k: data[k] for k in ("tiers", "order") if k in data})
            with LOG.open("a") as f:
                f.write(json.dumps(entry) + "\n")
        elif self.path == "/undo":  # returns the removed judgment (the tier page restores it)
            lines = LOG.read_text().splitlines(keepends=True) if LOG.exists() else []
            LOG.write_text("".join(lines[:-1]))
            return self.send((lines[-1].strip() if lines else "null").encode(), "application/json")
        else:
            return self.send(b"not found", "text/plain", 404)
        self.send(b"ok", "text/plain")

    def log_message(self, *args):
        pass


if __name__ == "__main__":
    POOL.mkdir(parents=True, exist_ok=True)
    url = f"http://localhost:{PORT}"
    print(f"ranking at {url} (Ctrl+C to stop); judgments -> {LOG}")
    webbrowser.open(url)
    ThreadingHTTPServer(("127.0.0.1", PORT), Handler).serve_forever()
