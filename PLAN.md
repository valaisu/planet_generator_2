# Plan

Goal: generate Earth-like planets in the Space_sceneries pixel-art style, judged
against pixel-art references made from real Earth photos (same framing + post).

- [x] Minimal base: raytraced sphere + sun + starfield + palette/dither post
      (copied from Space_sceneries / planet_generator, no planet generation).
- [x] Reference targets: `references/` = the 4 photos in `earth_images/` stylized
      into the same 128px frame with per-image k-means palettes.

## Simulation-based generator (agreed 2026-10-30)

Approach: simulate *outcomes*, not physics (Frozen Fractal "Around the World",
davidar sim-glsl). Everything lives on one cube-sphere grid (6 x N^2 cells) that the
renderer samples bilinearly — no mesh/nearest-vertex bake. Static plates for now.

- [x] 1. Cube-sphere grid (S2 tangent projection, neighbour tables, bilinear sample,
      least-squares gradient/divergence) + equirect debug-map dumps (`--maps`).
- [x] 2. Continents + static plates: warped Voronoi regions, continents grown from
      regions to a land target, plates with Euler-pole velocities, boundary
      classification -> collision ranges / Andes / trenches / island arcs / rifts /
      ridges, shelves, sea level by percentile.
- [x] 3. Erosion: priority-flood + implicit stream-power (Braun-Willett) on land.
- [x] 4. Climate: temperature (latitude, lapse, continentality, gyre anomaly),
      Earth-fitted pressure belts -> winds (+Coriolis, equatorial easterlies),
      moisture evaporated + advected, rain-out by convergence/uplift/convection,
      Hadley subsidence.
- [x] 5. Surface colour: Whittaker-style (T, P) blend over photo-calibrated biomes,
      in-biome noise, ice/snow, ocean depth/shelf/shallows, sea ice.
- [x] 6. Clouds from climate (humidity, rain, storm tracks, cold-current decks)
      with noise streaked along the wind + cyclone swirls.
- [x] 7. Rendering: relief-shaded normals, earth_2-like side sun, single-scattering
      Rayleigh+Mie atmosphere, per-image k-means palette.
- [x] 8. Verification on real Earth (`--earth data/earth_relief_1deg.f32`): deserts
      and rainforests land in the right places (see renders/earth_maps/).

## Tuning round 1 (agreed 2026-10-30)

- [x] Palette: 18 colours, split by what each pixel shows (space 2, land 8, ocean
      4, cloud 4); `--plain-palette` keeps one global k-means for comparison
      (user is sceptical of the split — difference turned out modest). References
      regenerated at 18 colours (plain k-means: photos have no material labels).
- [x] Land colour: saturated/separated biome colours, less brightness noise,
      haze halved looking straight down (full strength at the limb).
- [x] Deeper blue ocean.
- [x] Clouds: flow-noise warped texture (folds/filaments), explicit spiral storms
      (comma tails, tropical eyes), thin cirrus layer.
- [x] Low Earth orbit view (`--view leo`, 400 km, 256x128): real-thickness
      atmosphere, cloud layer ~8 km up with cloud shadows, render-time detail
      (fractal coasts, sub-grid relief, cloud-top lumps, land texture).

## Tuning round 2 (2026-10-30)

- [x] Clouds from a weather simulation (barotropic vorticity + stirring, cloud
      tracer carried by the simulated wind, advected texture) instead of
      hand-made swirls/spirals; scattered cumulus between systems.
- [x] LEO grain: removed sub-pixel detail noise; cloud tops at a height
      consistent with the atmosphere (whiter).
- [x] Colours: saturation x1.3 grade before the palette crush.

## Tuning round 3 (2026-10-30)

- [x] Clouds measured against the NASA cloud map (`tools/cloud_stats.py`) and
      rebuilt as a bounded cascade on the weather tracer: opacity mix, spectral
      slope and latitude profile now match real clouds; stays fully procedural
      (user wants an approach that generalises to other planet types).
- [x] `renders/` is regenerated as a whole by `tools/refresh_renders.sh`.

## Tuning round 4 (2026-10-30)

- [x] Elevation: no more narrow sea channels through land lined with ranges
      (ocean regions boxed in by continents are filled; continental rifts are
      broad valleys that stay mostly above sea level).
- [x] Colours measured against leo_0 / earth_2 (`tools/color_stats.py`): per-view
      exposure, less haze over the ground in LEO, olive land, teal-navy ocean,
      white (not cream) clouds.
- [x] LEO detail: domain-warped sub-grid coasts, terrain-following land texture
      (hollows wet/green, rises pale), lakes in wet lowlands, patchy turquoise banks.
- [x] LEO clouds: grid holds the coarse cascade, renderer adds the fine octaves;
      thin cloud breaks into clustered cumulus; views aim at partly cloudy coasts.

## Tuning round 5 (2026-10-30)

- [x] LEO clouds: cells smeared along the simulated wind (streets/bands),
      domain-warped edges, a ~2-3 px cumulus scale.
- [x] Rivers: drainage network weighted by runoff (Turc-Pike), drawn as
      meandering lines in LEO (Nile/Mississippi verified on real relief), green
      strips through dry land.
- [x] Beaches + cyan water: per-cell sandiness (gentle, unfrozen, drier coasts)
      -> beach strip + turquoise over sand bottoms; silt plumes off big rivers;
      own palette class so they survive the 18 colours.
- [x] Cities: habitability (climate, flat lowland, coasts/rivers, settlement
      history) -> 12000 Zipf-sized towns; night lights (`--sun-az` disk,
      `--sun-elev` LEO); renders/night_sheet.png, leo_night_sheet.png.
- [x] Fix: sub-grid relief no longer floods inland lowlands / sprinkles islands
      on shelves (coastline may only move within ~60 km of the grid coast).

## Animation (2026-10-30)

- [x] Orbit animation: `--frames/--orbits` (disk view, sun fixed, shared palette),
      `tools/animate.sh` -> GIF/MP4; in `renders/orbit_seed3.*`.
- [x] Moving clouds: the weather sim keeps running during animations, two
      cross-faded texture phases so patterns change.
- [x] Slow, realistic cloud motion: `--cloud-minutes` calibrated per planet to
      ~15 m/s drift, no sub-pixel octaves in disk animations, 1 slow orbit;
      separate cloud demo `renders/clouds_seed3.*` (fixed camera, 80 h).
- [x] LEO fly-along (`--view leo --frames N --km-per-frame 1`, clouds frozen),
      longer animations (30 s orbit / fly-along, 15 s cloud demo),
      `--vibrance` + `renders/vibrance_sheet.png` to pick a colour level.
- [x] Vibrance: user kept the default 1.3.
- [x] Disk colours (was too grey): the ~6x exaggerated atmosphere hazed the whole
      disk. Disk now uses the real-thickness atmosphere (like LEO) with haze 0.3;
      the thick shell only draws the limb glow (`View::rim_scale`).

## Next

- [ ] Clouds: small-scale detail still ~70% of real (0.15 vs 0.215); clouds read a
      little grey/thin next to the references' bright cores.
- [ ] Runtime ~10 s/planet (weather sim); fine for now, could drop flow steps.
- [x] LEO references: `references/leo_0` (Florida), `leo_1` (Sahara coast).
- [ ] LEO: 18-colour palette flattens the land texture the raw render has
      (`--raw` shows it).
- [ ] Climate on real Earth: eastern US far too dry, Europe too cold/wet (no
      Gulf Stream warmth), India dry (no monsoon) — cities follow the simulated
      climate, so these regions get too few lights on `--earth` runs.
- [ ] Night: faintest stars still dither toward the light orange.
- [ ] Optional: ocean sun glint, roads between cities, cirrus layer.
- [ ] Optional animation extra: a LEO fly-along (per-frame camera paths).
- [ ] Seed 3's simulated winds are ~4x seed 5's (mean |u| 6.5 vs 1.4); the
      stirring may over-energise some planets (stills look fine).
- [ ] Optional: moving plates (Cortial et al. 2019 / davidar) if static plates
      get too tame; baroclinic (two-layer) weather instead of stirring.
- [ ] Generalise beyond Earth: cascade/climate/weather parameters per planet type.

## Physics-based planets (started 2026-10-31)

- [x] Parameter list `PLANET_PARAMETERS.md`; suggested first set marked (59, rocky /
      icy bodies, ~12 true inputs). User to confirm / adjust.

Approach: a few physical **inputs** -> a `derive()` step computes everything else
in dependency order -> the existing pipeline consumes the derived values instead
of Earth constants. Earth stays a preset and a regression test (`--earth`, seed 3
renders should barely change). Solar-system bodies are the ground truth, the way
Earth photos were: real inputs in, the real body should come out.

### Phase A: references
- [ ] Stylize `solar_system_reference/` with `stylize_ref` like `references/earth_*`.
      Clean first: venus has a white bar on the left, pluto a white frame + caption,
      titan is a watermarked **false-colour infrared** mosaic (true-colour Titan is a
      featureless orange haze ball: replace it), europa/pluto are colour-enhanced.

### Phase B: parameters + derivation (numbers only, no rendering)
- [ ] `PlanetParams` (inputs) + `derive()` -> `PlanetPhysics` (derived), one file.
      Order: star -> insolation; mass + composition -> radius, g, v_esc;
      age + mass + tides -> internal heat -> tectonic regime, volcanism, surface age,
      magnetic field; escape + outgassing -> pressure + gases; albedo + greenhouse
      -> surface T (iterate: T changes ice/clouds/albedo); phase diagrams at (T, P)
      -> which liquid / ices; chemistry -> surface materials; life -> pigment.
- [ ] `--describe` prints the derived "planet card".
- [ ] Presets with real inputs: Mercury, Venus, Earth, Mars, Europa, Titan, Pluto.
      Check derived values against reality in a table (T, P, liquid, regime,
      field...). Simple, honest models; where a body defies them (Venus no plates,
      Mars' lost field) note it, don't over-fit.

### Phase C: pipeline driven by physics (one stage at a time, Earth regression each step)
- [ ] Terrain: regime-dependent (plates vs stagnant lid: big shield volcanoes,
      hemispheric dichotomy, no continents); relief scaled by gravity; sea level
      from liquid amount (zero = no ocean).
- [ ] Craters: new stage, density from surface age; fresh craters with ray
      systems (Mercury); erased by erosion/volcanism.
- [ ] Erosion: scaled by precipitation; off on airless bodies.
- [ ] Climate: replace Earth tables with parametric forcing (insolation, greenhouse,
      redistribution); cell count / jet latitudes from rotation; tidally locked mode.
- [ ] Clouds / weather: condensable from phase diagram (water, CH4, H2SO4, CO2);
      amount from the climate; Venus = full deck, Mars = rare thin.
- [ ] Surface colour: a **material model** (basalt, rust, salts, ices N2/CH4/CO2/H2O,
      tholins, sulfur, lava, vegetation with star-dependent pigment) mixed by derived
      fractions, replacing the Earth biome table for lifeless worlds.
- [ ] Atmosphere render: Rayleigh from composition + pressure, coloured aerosols
      (dust, tholin, sulfuric haze) via Mie, star colour, opaque mode for thick ones.
- [ ] Palette split: material classes generalised (no "ocean" on Mars).

### Phase D: solar system one by one (each adds one capability)
- [ ] Mercury (airless, craters, rays) -> Mars (thin dusty air, rust, caps, relict
      rivers) -> Venus (opaque cloud deck) -> Europa (ice shell, crack lines, tinted
      salts) -> Titan (orange haze) -> Pluto (N2/CH4 ice plains, tholin red).
      Comparison sheet: preset render next to its reference.

### Phase E: new planets
- [ ] Sample inputs from plausible distributions -> derive -> render; sheet of random
      planets; reject or flag impossible combinations.
- [ ] Later: gas giants, rings, moons in frame, seasons/animations, binary stars.
