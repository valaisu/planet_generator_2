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
uv run --no-project --with numpy --with pillow python tools/color_transfer.py mars_800.png solar_system_reference/mars.jpg mars_ct.png   # prototype: render = structure, photo = colours (render with --raw --vibrance 1)
