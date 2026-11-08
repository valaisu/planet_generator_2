"""Click-to-pick app for variant sheets: shows every OUT_tiles/ folder written by
tools/variants.py or tools/random_sheet.py (under renders/), one section per sheet.
Click tiles to select them, add a note per sheet; every change is saved to
renders/variants/picks.json (which Claude reads).
    uv run --no-project python tools/picker.py            # opens http://localhost:8765
"""
import json
import sys
import webbrowser
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent / "renders"
PICKS = ROOT / "variants" / "picks.json"
PORT = int(sys.argv[1]) if len(sys.argv) > 1 else 8765

PAGE = """<!doctype html><html><head><meta charset="utf-8"><title>Planet picker</title>
<style>
body{background:#0c0c10;color:#e6e6eb;font:14px system-ui,sans-serif;margin:16px}
h2{margin:28px 0 4px;font-size:16px} .hint{color:#8a8a96;margin:0 0 10px}
.grid{display:flex;flex-wrap:wrap;gap:10px}
.tile{width:200px;cursor:pointer;border:3px solid transparent;border-radius:6px;padding:4px;background:#16161c}
.tile img{width:200px;height:200px;image-rendering:pixelated;display:block}
.tile.on{border-color:#4cc38a;background:#13261d}
.id{font-weight:600} .label{color:#9a9aa6;font-size:12px;word-break:break-word}
textarea{width:min(820px,100%);height:44px;background:#16161c;color:#e6e6eb;border:1px solid #333;border-radius:4px;margin-top:8px}
#status{position:fixed;top:8px;right:12px;color:#4cc38a}
</style></head><body>
<h1>Planet picker</h1><p class="hint">Click tiles to pick (click again to unpick). Saved automatically to renders/variants/picks.json.</p>
<div id="status"></div><div id="sheets"></div>
<script>
let picks = {};
async function save(){
  await fetch('/picks',{method:'POST',body:JSON.stringify(picks)});
  const s=document.getElementById('status'); s.textContent='saved'; setTimeout(()=>s.textContent='',1200);
}
async function main(){
  const [sheets, saved] = await Promise.all([fetch('/sheets').then(r=>r.json()), fetch('/picks').then(r=>r.json())]);
  picks = saved;
  const root = document.getElementById('sheets');
  for (const sh of sheets){
    const p = picks[sh.name] = picks[sh.name] || {picked:[], note:''};
    const sec = document.createElement('section');
    sec.innerHTML = `<h2>${sh.name}</h2>`;
    const grid = document.createElement('div'); grid.className='grid';
    for (const t of sh.tiles){
      const d = document.createElement('div'); d.className='tile'+(p.picked.includes(t.id)?' on':'');
      d.innerHTML = `<img src="/img/${sh.dir}/${t.file}" loading="lazy"><div class="id">${t.id}</div><div class="label">${t.label}</div>`;
      d.onclick = ()=>{ const i=p.picked.indexOf(t.id);
        if(i<0) p.picked.push(t.id); else p.picked.splice(i,1);
        d.classList.toggle('on'); save(); };
      grid.appendChild(d);
    }
    const note = document.createElement('textarea'); note.placeholder='Note for this sheet (optional)'; note.value=p.note;
    note.oninput = ()=>{ p.note=note.value; clearTimeout(note.t); note.t=setTimeout(save,600); };
    sec.appendChild(grid); sec.appendChild(note); root.appendChild(sec);
  }
  if (!sheets.length) root.textContent = 'No *_tiles folders under renders/ yet.';
}
main();
</script></body></html>"""


def sheets():
    out = []
    for idx in sorted(ROOT.rglob("*_tiles/index.json")):
        d = idx.parent
        out.append({"name": d.name[:-6], "dir": str(d.relative_to(ROOT)), "tiles": json.loads(idx.read_text())})
    return out


class Handler(BaseHTTPRequestHandler):
    def send(self, body, ctype, code=200):
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        if self.path == "/":
            self.send(PAGE.encode(), "text/html; charset=utf-8")
        elif self.path == "/sheets":
            self.send(json.dumps(sheets()).encode(), "application/json")
        elif self.path == "/picks":
            self.send(PICKS.read_bytes() if PICKS.exists() else b"{}", "application/json")
        elif self.path.startswith("/img/"):
            f = (ROOT / self.path[5:]).resolve()
            if ROOT.resolve() in f.parents and f.suffix == ".png" and f.exists():
                self.send(f.read_bytes(), "image/png")
            else:
                self.send(b"not found", "text/plain", 404)
        else:
            self.send(b"not found", "text/plain", 404)

    def do_POST(self):
        if self.path != "/picks":
            return self.send(b"not found", "text/plain", 404)
        data = json.loads(self.rfile.read(int(self.headers["Content-Length"])))
        PICKS.parent.mkdir(parents=True, exist_ok=True)
        PICKS.write_text(json.dumps(data, indent=1))
        self.send(b"ok", "text/plain")

    def log_message(self, *args):
        pass


if __name__ == "__main__":
    url = f"http://localhost:{PORT}"
    print(f"picker at {url} (Ctrl+C to stop); picks -> {PICKS}")
    webbrowser.open(url)
    ThreadingHTTPServer(("127.0.0.1", PORT), Handler).serve_forever()
