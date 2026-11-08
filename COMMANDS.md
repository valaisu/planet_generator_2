# Commands

Build (first time / after CMakeLists changes, then after edits):

```bash
cmake -S . -B build && cmake --build build -j
```

Generate + render a planet (writes `earth.png` + a 4x upscaled `earth_x4.png`):

```bash
./build/earthgen --seed 3 --out earth.png
./build/earthgen --seed 3 --maps maps/ --out earth.png     # + equirect maps of every field (incl. rivers_cities.png)
./build/earthgen --seed 3 --raw --yaw 90 --out raw.png     # no pixel-art post, rotated view
./build/earthgen --seed 3 --plain-palette --out plain.png  # one global k-means instead of the per-material split
```

Low Earth orbit view (400 km, 256x128; looks at a seeded coastline unless aimed):

```bash
./build/earthgen --seed 3 --view leo --out leo.png
./build/earthgen --earth data/earth_relief_1deg.f32 --view leo --lat 25 --lon -100 --heading 90 --out leo_mexico.png
```

Night side with city lights (disk: turn the sun away; LEO: sun below the horizon):

```bash
./build/earthgen --seed 3 --sun-az 150 --out night.png
./build/earthgen --seed 3 --view leo --sun-elev -15 --out leo_night.png
```

Orbit animation (camera circles the planet, sun fixed; the weather keeps running so
clouds drift and change; one shared palette; ~70 s for 240 frames)
-> `orbit.gif`, `orbit.mp4`, frames in `orbit_frames/`:

```bash
tools/animate.sh orbit 720 24 --seed 3 --orbits 1 --cloud-minutes 20     # OUT_STEM FRAMES FPS [earthgen args]
tools/animate.sh clouds 360 24 --seed 3 --orbits 0 --cloud-minutes 20    # cloud demo: fixed camera
tools/animate.sh renders/spin/20121 192 24 --random 20121 --spin --orbits 0.3 --sky-speed 0.067 --cloud-minutes 4.3  # planet spins under a fixed sun (0.3 turns in 8 s), sky at 0.067x the planet; cloud-minutes = rotation_h*60*orbits/frames
uv run --no-project python tools/spin.py --n 10     # spinning clips of the 10 most distinct showcase picks -> renders/spin/
uv run --no-project --with pillow python tools/leo_showcase.py --n 20   # low-orbit views of 20 showcase picks -> renders/leo_showcase/sheet.png
tools/animate.sh leo_fly 720 24 --seed 3 --view leo --km-per-frame 1     # LEO fly-along (~0.7 s/frame)
./build/earthgen --seed 3 --frames 120 --cloud-minutes 0 --out orbit.png       # frames only, frozen clouds
```

Colour vibrance (saturation boost before the palette; default 1.3) and a sheet to compare levels:

```bash
./build/earthgen --seed 3 --vibrance 1.9 --out vivid.png
tools/vibrance_sheet.sh renders/vibrance_sheet.png 3 1.0 1.3 1.6 1.9 2.2 2.5   # OUT SEED LEVELS...
```

Run the simulation on real Earth relief (verification; yaw -135 = Australia, 80 = Americas):

```bash
./build/earthgen --earth data/earth_relief_1deg.f32 --yaw -135 --maps maps/ --out earth_real.png
```

Contact sheet: seeds 1..N next to the 4 references:

```bash
tools/sheet.sh renders/sheet.png 8
```

Regenerate the whole `renders/` folder (wipes it first; see renders/README.txt):

```bash
tools/refresh_renders.sh
```

Cloud statistics vs the real NASA cloud map (needs `--maps` output):

```bash
./build/earthgen --seed 2 --maps maps/ --out earth.png
uv run --no-project --with numpy --with pillow python tools/cloud_stats.py data/cloud_combined_2048.jpg maps/cloud_alpha.png
./build/earthgen --seed 2 --cloud-image data/cloud_combined_2048.jpg --out real_clouds.png   # diagnostic: real clouds
```

Colour statistics per material (space/cloud/ocean/land) of a photo (optional crop) vs a raw render:

```bash
./build/earthgen --seed 3 --view leo --raw --out leo_raw.png
uv run --no-project --with numpy --with pillow python tools/color_stats.py earth_images/leo_0.jpeg:0,100,678,339 leo_raw.png
```

Weather-sim diagnostics (max wind, solver residual, curl/divergence checks every 50 steps):

```bash
WEATHER_DEBUG=1 ./build/earthgen --seed 2 --out earth.png
```

Tune the rain-out coefficients without rebuilding (see climate.cpp for meaning/order):

```bash
CLIMATE_DEBUG=1 CLIMATE_K=0.003,0.012,0.3,0.012,0.7,0.5,3 ./build/earthgen --earth data/earth_relief_1deg.f32 --maps maps/
```

Rebuild every reference from the photos (Earth, low orbit, solar system + `solar_system_reference/extra/`;
paletted in `references/`, palette-free in `references/raw/`; disk circles are in the script):

```bash
tools/make_references.sh
uv run --no-project --with numpy --with pillow --with scipy python tools/fit_disk.py photo.jpg 0.06   # circle for a new photo
```

Compare renders with the photos, by numbers (detail per scale 1-16 px, feature sparsity, colour
distribution) — whole set, one body, material colours, knob ranking:

```bash
tools/eval_all.sh                                   # -> renders/eval/<body>_{raw,pal}.png + summary.txt
uv run --no-project --with numpy --with pillow --with scipy --with matplotlib python tools/evaluate.py out.png \
    --ref references/raw/mars.png references/raw/mars_osiris.png --render mars_raw.png
uv run --no-project --with numpy --with pillow --with scipy python tools/endmembers.py references/raw/mars.png
uv run --no-project --with numpy --with pillow --with scipy python tools/tune.py mars --refs references/solar/mars.png \
    references/extra/mars_osiris.png --base "--sun-az 43" --grid "auto_p95=0.55,0.65" --grid "dark_amount=0.32,0.45"
```

Physics: planet card of a preset (mercury venus earth mars europa titan pluto), inputs
can be overridden; derived-vs-real table for all presets; `PHYSICS_DEBUG=1` traces the
climate iteration:

```bash
./build/earthgen --planet mars --describe
./build/earthgen --planet earth --star-mass 0.3 --orbit-au 0.11 --describe   # Earth around an M dwarf
./build/earthgen --check-presets
```

Render a preset from its physics (terrain style, craters, air, clouds, materials all derived),
and the comparison sheet of all presets under their reference photos:

```bash
./build/earthgen --planet mars --out mars.png
./build/earthgen --planet earth --star-mass 0.3 --orbit-au 0.11 --out mdwarf.png   # any input override works
tools/solar_sheet.sh solar_sheet.png
uv run --no-project --with pillow python tools/pair_sheet.py renders/pair_sheet.png   # each reference | generated, labelled
uv run --no-project --with numpy --with pillow python tools/palette_compare.py pal.png references/solar/mars.png renders/planets/mars.png   # colours present + disk share
uv run --no-project --with numpy --with pillow --with scipy python tools/detail_stats.py references/solar/mars.png renders/planets/mars.png   # 1-3 px detail, contrast, mean colour
CRATER_DEBUG=1 ./build/earthgen --planet mercury --out mercury.png   # crater counts / ray coverage
DUST_DEBUG=1 ./build/earthgen --planet mars --maps maps/ --out mars.png   # wind-blown dust per latitude band; maps/wind_dust.png
./build/earthgen --planet mars --world dust_texture=1,dust_window=150 --out mars.png   # flow-shaped dust: texture strength / stretch
```

New planets from random physical inputs (seed = the planet; overrides still apply), and a
labelled sheet of a seed range:

```bash
./build/earthgen --random 7 --describe
./build/earthgen --random 7 --out new.png
./build/earthgen --planet earth --impact-myr 1 --volatiles 1e-7 --out molten.png   # history: a dry Earth 1 Myr after a giant impact
./build/earthgen --planet earth --snowball --out snowball.png                      # climate started frozen over
uv run --no-project --with pillow python tools/random_sheet.py renders/random_sheet.png 1 24   # OUT FIRST COUNT (~3 min)
uv run --no-project --with pillow python tools/random_sheet.py out.png 39,101,73 "--world lava_steps=100"   # a list of seeds (+ extra args)
uv run --no-project --with pillow python tools/random_sheet.py renders/demo.png 39,245,177,197,278,247,101,338,192,73,113,96,164,227,63,45,97,238,2,153,47,54,62,129   # demo: 24 world types (~6 min)
uv run --no-project python tools/picker.py   # click-to-pick app for all sheets (renders/**/_tiles) -> renders/variants/picks.json
```

Preference ranking: pre-render a pool of random planets (incremental, resumable, ~450/hour),
then sort batches of ~40 into tiers S-D by drag and drop (main page; /rank = rank sets of up to 6,
/ranking = the Bradley-Terry ranking). Judgments go to `pool/comparisons.jsonl`
(kept in git; the pool lives outside `renders/` because refresh_renders.sh wipes that):

```bash
uv run --no-project python tools/pool.py pool 1 2000           # DIR FIRST COUNT
uv run --no-project python tools/pool.py pool --balanced 8 1 3000   # up to 8 per world type from seeds 1-3000 (rare types too)
uv run --no-project python tools/pool.py --census 1 3000             # how common each world type is (seconds)
uv run --no-project python tools/pool.py pool --from views/index.json  # the same planets as the DINO views (judgment <-> embedding)
uv run --no-project python tools/rank.py                        # http://localhost:8766
uv run --no-project python tools/rank.py pool_v1                # the first pool (before the ice/rust colour change) + its judgments
```

DINOv2 fine-tuning (label-free) on several views of each planet: render the views (4 x 224 px raw,
~8 s per planet), fine-tune + evaluate (GTX 1050 Ti, ~1 min per epoch at 750 planets), then a sheet of
nearest neighbours (frozen vs fine-tuned). Outputs in `models/`, log in `models/dino_vits14.log`:

```bash
uv run --no-project python tools/views.py views 1 6000 40      # DIR FIRST COUNT PER_TYPE (pool seeds first)
TORCH="uv run --no-project --index https://download.pytorch.org/whl/cu126 --with torch==2.14.1 --with torchvision --with pillow --with numpy"
$TORCH python tools/dino.py train --epochs 30
$TORCH python tools/dino.py sheet                                # -> renders/dino/neighbors_vits14.png
```

What predicts the tiers (world type, generation inputs, physics card, DINO embeddings; cross-validated
pairwise accuracy + noise ceiling + feature importance -> `models/prefs_results.txt`, ~30 s):

```bash
uv run --no-project --with numpy --with scikit-learn python tools/prefs.py
```

N liked-and-different new planets (preference models from the tiers + DINO distance, greedy MMR;
anything predicted B or better is eligible, variety decides; ~25 min for 160 candidates;
`showcase/` is pool-format, so `rank.py showcase` can tier them):

```bash
$TORCH --with scikit-learn python tools/showcase.py                       # -> renders/showcase/showcase.png
$TORCH --with scikit-learn python tools/showcase.py --n 42 --diversity 0.8 --min-tier 1.5   # more variety, C+ allowed
```

Variant sheets for picking by eye (visual-options skill): `--world key=value,...` overrides
any WorldSpec knob (scarps, scarp_height, streaks, streak_strength, shading, mottle, roughness,
crater_density, crater_relief, dark_amount, stain_amount, slope_albedo, ...); `--grid` combines them:

```bash
./build/earthgen --planet mars --world shading=2,scarp_height=3 --out mars.png
uv run --no-project --with pillow python tools/variants.py renders/variants/mars.png --base "--planet mars" \
    --ref references/solar/mars_x4.png --grid "shading=1,2,4" --grid "scarp_height=0,1,3,6"
```

Re-download / convert the Earth relief data (one-off):

```bash
curl -L -o data/earth_relief_01d_g.grd https://oceania.generic-mapping-tools.org/server/earth/earth_relief/earth_relief_01d_g.grd
uv run --no-project --with h5py --with numpy python tools/convert_relief.py data/earth_relief_01d_g.grd data/earth_relief_1deg.f32
```
./build/earthgen --planet mars --size 800 --raw --vibrance 1 --sun-az 43 --out mars_800.png   # full-res raw render to compare with solar_system_reference/mars.jpg ([--grid 1024]: same planet + smaller craters)
./build/earthgen --planet mars --world relief_light=0 --out mars.png   # physical relief light (Mars default: mosaic-style, relief_light=1)
uv run --no-project --with pillow python tools/variants.py out.png --tile 400 --base "--planet mars --size 800 --raw" --grid "shading=1,3"   # 800 px option sheet
uv run --no-project --with numpy --with pillow --with scipy --with matplotlib python tools/color_transfer.py mars_800.png solar_system_reference/mars.jpg mars_ct.png   # prototype: render = structure, photo = colours (render with --raw --vibrance 1)
uv run --no-project --with numpy --with pillow --with scipy --with matplotlib python tools/color_transfer.py --save-profile solar_system_reference/mars.jpg data/profiles/mars.prof   # photo colour profile earthgen applies (WorldSpec::color_profile)
tools/loop_sheet.sh renders/loop/iter09.png [--world k=v]   # Mars iteration sheet: photo | 800 px faces (+ _128.png pixel art vs reference)
