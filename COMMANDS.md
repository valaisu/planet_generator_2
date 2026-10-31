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

Rebuild the pixel-art reference targets from the photos (writes `.png`, `_x4.png`, `.pal`):

```bash
for f in Earth_0 earth_1 earth_2 earth_3; do ./build/stylize_ref earth_images/$f.jpg references/${f,,}.png; done
# low-orbit photos (256x128 frame, cropped around the ISS hardware):
./build/stylize_ref earth_images/leo_0.jpeg references/leo_0.png --frame --crop 0,100,678,339
./build/stylize_ref earth_images/leo_1.jpeg references/leo_1.png --frame --crop 210,200,860,430
```

Re-download / convert the Earth relief data (one-off):

```bash
curl -L -o data/earth_relief_01d_g.grd https://oceania.generic-mapping-tools.org/server/earth/earth_relief/earth_relief_01d_g.grd
uv run --no-project --with h5py --with numpy python tools/convert_relief.py data/earth_relief_01d_g.grd data/earth_relief_1deg.f32
```
