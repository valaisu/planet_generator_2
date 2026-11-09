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
      icy bodies, ~12 true inputs).

Approach: a few physical **inputs** -> a `derive()` step computes everything else
in dependency order -> the existing pipeline consumes the derived values instead
of Earth constants. Earth stays a preset and a regression test (`--earth`, seed 3
renders should barely change). Solar-system bodies are the ground truth, the way
Earth photos were: real inputs in, the real body should come out.

### Phase A: references
- [x] Stylize `solar_system_reference/` with `stylize_ref` like `references/earth_*`
      -> `references/solar/` (+ `sheet.png` with earth_2). Titan replaced by Cassini
      natural colour, Pluto by New Horizons true colour (Wikimedia, NASA PD; old files
      kept as `*_infrared_falsecolor.jpg` / `*_enhanced_poster.jpg`); Europa kept
      (the "natural colour" download was the same Galileo frame). Partly dark limbs
      (Mercury, Venus, Titan, Pluto) framed with `tools/fit_disk.py` -> `--circle`.
      Known: the space background dithers toward planet colours (red around Mars).

### Phase B: parameters + derivation (numbers only, no rendering)
- [x] `PhysicalInputs` (inputs; `PlanetParams` was taken by the generator knobs) +
      `derive()` -> `PlanetPhysics`, in `src/physics.cpp`. Suggested 59-parameter set
      from PLANET_PARAMETERS.md used as is.
      Order: star -> insolation; mass + composition -> radius, g, v_esc;
      age + mass + tides -> internal heat -> tectonic regime, volcanism, surface age,
      magnetic field; escape + outgassing -> pressure + gases; albedo + greenhouse
      -> surface T (iterate: T changes ice/clouds/albedo); phase diagrams at (T, P)
      -> which liquid / ices; chemistry -> surface materials; life -> pigment.
- [x] `--describe` prints the derived "planet card".
- [x] Presets with real inputs: Mercury, Venus, Earth, Mars, Europa, Titan, Pluto.
      `--check-presets` table: T within 10 K everywhere (except Europa 92 vs 102,
      Pluto 36 vs 40), P right to ~25% (Venus 73 vs 92), albedo, regime, field,
      liquids right. Calibrations: Earth (greenhouse, eq-pole contrast, volatile
      ratios), Venus (CO2 opacity), Mars (escape slope), Titan (N2 ratio, CIA).
      Known misses, left as is: Mercury locks 1:1 (real 3:2, no eccentricity input),
      Venus N2 0.66 bar (real 3.2), Pluto albedo 0.56 (real 0.72), Titan's lakes
      not polar (no eq-pole contrast in a 1.5-bar slow rotator), Earth CO2 690 ppm.
      Behaviour checks: Earth runs away between 0.95 and 0.90 AU, the thermostat
      holds 288 K to 1.5 AU, an M-dwarf Earth locks and loses its air. Caveat: a
      5 M_E Earth gets 4.7 bar N2 (same volatile fraction) and runs away at 1 AU
      (pressure-broadened water vapour; real sensitivity uncertain).

### Phase C: pipeline driven by physics (one stage at a time, Earth regression each step)
Done 2026-10-31 via `WorldSpec` (planet.h, `P.world`), filled by `world_from_physics`;
`--planet NAME` renders a preset. Defaults = Earth: plain `--seed 3` renders stayed
byte-identical (disk raw, disk paletted, LEO raw) after every step.
- [x] Terrain: regime-dependent styles (plates as before; stagnant lid = dichotomy +
      volcanic rise + shields; dead = undulation + smooth plains; ice shell = lineae /
      N2-ice basin with ringed mountains); relief from gravity + size (Earth-equivalent
      metres); no ocean -> no sea level.
- [x] Craters (`craters.cpp`): density from surface age x impact flux (falls with
      distance), D^-2 sizes, bowls / flat floors + central peaks, degraded with age,
      fewer on resurfaced plains, rays on the youngest big ones.
- [x] Erosion: only with a water ocean (off on airless / dry worlds).
- [x] Climate: dry worlds use the physics' latitude profile (or day/night side when
      locked to the star); ocean worlds shift Earth's curve by the derived T. Jet
      latitude of the weather sim from the Hadley edge (Earth 45, Venus/Titan 70).
      Not done: Earth's pressure tables / rain for non-Earth ocean worlds, methane rain.
- [x] Clouds: amount from the physics; none = sim skipped; Venus / Titan = opaque deck
      shaded by the simulated weather.
- [x] Surface colour: material model for lifeless worlds (base, dark patches, stains =
      Europa cracks/chaos or Pluto tholin belt, rays, polar / volatile ice).
- [x] Atmosphere render: Rayleigh from composition + pressure, scale height,
      coloured aerosols (dust, tholin, sulfuric), star tint, airless = none; rim glow
      capped for tall atmospheres.
- [x] Palette split: absent classes (no ocean, no clouds, ground under a deck) give
      their colours to the biggest one.
- Comparison sheet: `tools/solar_sheet.sh` (also in `renders/solar_sheet.png`).
  Open for phase D: Mars too dark/saturated, its dark markings blobby; Pluto's
  tholin belt too small; Mercury lacks the fine bright speckle; Europa's lineae
  too few/straight; the studio light (front-right) shows a crescent the photos lack.

### Detail round (2026-10-31, user feedback: small detail first, landmarks subtle, generalize)
- [x] Craters down to ~1.5 cells (half a disk pixel, read as texture), N(>D) ~ D^-1.8,
      basins <= 0.25 R, big/old craters relaxed; icy bodies x0.35 crater depth (ice
      flow + frost mantling).
- [x] Albedo coupled to terrain by process (`slope_albedo`, `crest_albedo`): airless
      slopes fresh/bright, dust on flats + dark slopes, frost on icy crests; mottling
      at 1-3 px (`mottle`); soft domain-warped regions instead of thresholded blobs.
- [x] Lower shields / gentler dichotomy, grid-scale roughness, 400 faint lineae,
      zonal streaks on decks, material colours (duller: renders get 1.3x vibrance).
- [x] `tools/detail_stats.py`: 1-3 px detail / contrast / mean colour per disk.
- [ ] Still open: Mars' dark regions weak; Pluto's bright plains + belt barely show;
      Europa greyish; Venus streaks faint; small detail still ~1.3x the references
      on Mercury/Pluto (crater shading).

### Detail round 2 (2026-10-31): discrete features instead of noise
- [x] Mottling + grid-scale roughness noise off (knobs `mottle`, `roughness`, default 0).
- [x] `--world key=value` knob overrides + `tools/variants.py` sheets; skill `visual-options`.
- [x] Lobate scarps (old crust) + wrinkle ridges (young plains) on rocky non-plate
      worlds (`scarps`, `scarp_height`); wind streaks behind craters on dusty worlds
      (`streaks`, `streak_strength`; too small to see at disk scale so far).
- [x] User picks: relief shading 1x, scarp height 1 (A2); Mars streaks 3 x strength 4 (B4).
      User: "none of this captures the essence of Mars".
- [x] Research (web): Mars at disk scale is albedo, not relief; bright = dust settled
      where wind is weak (high, thin-air plateaus), dark = wind-scoured basaltic sand.
      -> dust-cover model for dusty worlds (`dust`, `dust_elev`, `dust_slope`, `comb`:
      wind stress from air density x slopes x wind belts, edges combed along the wind),
      dark contrast knobs `dark_mix`, `dark_level`.
- [x] User pick: dark contrast A3 (dark_mix 1) for dusty worlds.
- [ ] Colour structure (user: "the images consist of different colours";
      `tools/palette_compare.py`): render palettes are one-hue luma ramps (one material
      x lighting); reference palettes have several hues/saturations at the same
      brightness (Mars: orange + mauve-grey; Europa: brown + grey-green + tan; Venus:
      greys + peach + bluish white). Renders also spend ~15-25% of the disk on black
      terminator shadow, clip highlights to cream/yellow-white (refs: neutral/cool
      white) and give blue rim/sky tones palette slots.
- [x] Colour round 1: third material at equal brightness (`alt`, `alt_amount`, per
      material identity), auto exposure for the brightest material (`exposure_target`,
      default 0.92 on lifeless worlds), shadow pixels share 2 palette tones (lifeless
      worlds; Earth's split unchanged). Sheets: renders/variants/colour_alt_material.png,
      colour_exposure.png, sun_angle.png (--sun-az > 0 = sun toward the camera).
- [ ] User picks from those three sheets.
- [ ] Next feature types (one at a time, as sheets): ice-shell crack network with
      terminations/families, channels from drainage on once-wet worlds, lava flow
      lobes, dune fields, polar layered deposits; detail density by geologic unit.

### Investigation: why the renders looked different from the photos (2026-10-31)
Tools: 10 extra true-colour photos (`solar_system_reference/extra/`, Wikimedia/NASA),
`tools/make_references.sh` (paletted + raw refs), `tools/evaluate.py` / `eval_all.sh`
(detail per scale, kurtosis, Lab colour), `tools/endmembers.py` (material colours),
`tools/tune.py` (ranks knob grids against all photos of a body). Findings, by cause:
- [x] **Lighting law.** Lambert shading darkens a sphere toward the limb; real regolith
      (single scattering) is ~Lommel-Seeliger: a full disk stays flat (full Moon). This
      alone was most of Mars' excess brightness spread / "regional" variation.
      -> `lommel_seeliger` = 1 - 0.6 x albedo on lifeless worlds.
- [x] **Detail of the wrong kind.** Renders put variation into small elements (crater
      dots/rims, fine streak noise, mottling); the photos into large coherent regions
      (Mars: no craters visible at all at disk scale). Kurtosis: photos 3-20 (sparse
      features), renders ~1-2 (Gaussian). Fixed: dust slope scouring off (dark crater
      dots), no frost on crests, Venus fine streaks -> broad swirls, Titan haze smooth
      (`deck_texture`), sharper contacts between units (`edge` 0.2). Exception found:
      Europa IS busy at small scales (dense lineae + mottling) -> 1200 lineae >= a grid
      cell wide, chaos at two scales. Airless worlds: small fresh craters keep bright
      ejecta (`fresh` 0.2) -> Mercury's speckle.
- [x] **Colour distribution.** Photos: colours on a material mixing line (Mars: dark
      grey-brown basalt <-> bright saturated dust; chroma rises with brightness), a
      separate pale branch only for ice/clouds/haze. Renders were one hue x lighting,
      too blue (Mars), too warm/dark (Venus), missing darks (Pluto). Material colours
      now measured on the photos (median of 1-3 photos, vibrance-compensated); invented
      third materials (mauve dust, blue CH4 frost) dropped (`alt_amount` 0).
- [x] **Exposure.** Photos are exposed for the disk (95th percentile ~0.66-0.99); keyed
      to the brightest material, renders were dark. -> camera-like `auto_p95`
      (0.72 surfaces, 0.9 decks; best fits per body 0.55-0.95).
- [x] Pluto: dark tholin terrain must cover far more (stain 0.9); Venus/Titan: no blue
      Rayleigh halo around decks.
- Metric distance (tools/tune.py, lower = closer), before -> after: Mars 4.4 -> 0.36,
  Pluto 11.4 -> 0.9, Mercury 1.2 -> 0.54, Titan 6.0 -> 3.4, Europa ~3 -> ~3 (its photos
  disagree: Galileo enhanced brown vs Juno neutral grey).
- [ ] User picks: renders/variants/exposure.png (per body), europa_lineae.png,
      mars_pattern.png (dust_belts x comb: zonal banding of Mars' dark regions).
- [x] pair_sheet.py / eval_all.sh turn the sun to match the photos' phase.
- [ ] Still open: Venus/Titan structure (photos' variation is mostly limb/haze lighting),
      Europa large-scale contrast, Mars dark-region shapes (zonal bands), Earth colours
      (L -10, b +5 vs photos) not touched.

### Mars round (2026-11-01; user: "no details, colours wrong, forgot ice")
- [x] Seasonal frost cap: condensable air + seasons -> the winter (south) polar night
      frosts over poleward of 90 - obliquity - margin (`frost_lat`; Mars ~60 deg).
- [x] Craters by terrain age: old crust carries the full record (density of a surface
      age - 0.5 Gyr), resurfaced plains keep only craters younger than themselves
      (`crater_density_young`); Mars' lowlands ~clean, highlands saturated.
- [x] Dark sand in hollows / dust on rims (`sand_lows`) -> craters read face-on;
      multi-scale dust variability (`dust_detail`); dust mantles craters (relief 0.7).
- [x] Canyon system + radial grabens around a volcanic rise (`canyons`, stagnant lid).
- [x] Colours matched on the paletted render vs the original photo (brick red, darker
      basalt), exposure 0.5 for dusty worlds, limb darkening from the atmosphere
      (Lommel-Seeliger x0.6 with air). tools/tune.py distance vs the original: 0.27.
- [x] User: all seeds bad (1/3/5 best colour balance); the noise patterns are wrong —
      thin air means the surface is shaped by turbulent flow.

### Wind-shaped dust (2026-11-01; user: "athmosphere -> turbulent flows")
Rule: a world with air shows turbulent shapes, in its clouds or (dry) on its surface.
- [x] The weather model (clouds.cpp) now also runs for dusty worlds and carries dust:
      eddy gusts (wind minus its zonal mean) x air density (thinner on high ground)
      lift the windiest `dust_lift` share per latitude band, the flow carries it, it
      settles in calm air, over the last `dust_steps` steps (`dust_step`). Gusts at
      every latitude on dusty worlds (stirring not only in storm tracks).
- [x] Fine dust texture = bounded cascade at flow-advected coordinates (as the clouds,
      window `dust_window` steps): filaments, feathered fronts, swirls. Replaces the
      noise-thresholded regions + straight comb (`dust_cover` keeps it as fallback).
- [x] User picks: between B1 and A3 of mars_flow.png, then C2/C3 of mars_soft.png
      (soft rank transition `dust_soft`) -> defaults texture 0.4, window 75, soft 0.4.
      User: Mars still "not great" but parked; all other bodies are good (Titan's plain
      yellow matches some photos). Open Mars leads: few big coherent dark provinces,
      colour variety at equal brightness (0.06x the photos), sub-pixel filaments.
- [ ] Later: decks (Venus/Titan) from the same flow; crater wind tails along the
      simulated wind instead of the zonal prevailing wind; spin rate -> eddy size.

### Beautiful Mars (proposed 2026-11-03; the photos are the gold standard)
Judged at 800 px raw (no palette) next to solar_system_reference/mars.jpg, then
checked that it survives the 128 px pixel art. Every step is a general rule.
- [x] 0. Same planet at any grid size (craters/terrain in km, not cells) so 800 px
      work (grid 1024) and the 128 px art (grid 256) show one planet. (Terrain was
      already grid-free; craters now drawn at the 256-grid size, finer grids add only
      smaller ones from their own stream. Grid 256 byte-identical.)
- [x] 1. Relief shading that reads face-on: `relief_light` (0 physical, 1 mosaic) +
      `relief_sun`; Mars mosaic, sun 40 deg, shading 1.5 (user pick B1), light from the
      screen's upper left (lit from the right, craters read as domes).
      Later: make physical lighting show relief (user wants physical eventually).
- [x] 2. Craters: irregular outlines (polygonal / scalloped when big), uneven breached
      rims, terraces, central peaks or pits, peak rings > 120 km, massif-ringed basins
      > 400 km (up to 0.7 R, rarer above 0.25 R), infill as a thickness (`crater_fill`:
      small old craters -> ghosts), rims outlast floors, rampart ejecta (`ejecta_lobes`),
      dark sand on the downwind floor (`P.crater_sand`).
- [x] 3. Ground (`ground_detail`, stagnant lid): knobby old crust, boundary mesas, wrinkle
      ridges (fbm3r: rotated octaves, no lattice facets). Not done: old valley networks.
- [x] 4. Albedo: swirl texture off (dust_texture 0); dust edges shaped by 40-100 km
      terrain; dark sand in lows (km-based blurs, any grid); dust blankets rays; streaks
      soft (no wedge facets). Removed: stress-noise/comb fallback + knobs dust_slope,
      comb, dust_belts, dust_detail; crest dust on rims (red outlines).
- [x] 5. Colours matched paletted vs paletted (the 1.3 vibrance grade overshoots raw
      matches): bright 0.71 0.40 0.28 / dark 0.33 0.22 0.18 vs photo 0.70 0.41 0.29 / 0.31 0.21 0.18.
- [~] 6. Limb: dust phase g 0.6 (`mie_g`), `mie_height` knob (2 greyed the limb: kept 1);
      the photo's pale limb glow is not reproduced. Frost follows pole-facing slopes + hollows.
- [x] 7. 128 px: craters mostly sub-pixel; other bodies unchanged by eye; Earth byte-identical.
- [x] Picks: A3 + A4 (crater_fill 1.5, sand_lows 1.2, dark_amount 0.55) are Mars defaults.
- [x] Looser colours tried as random regional tints (`color_variety`): rejected (invented
      colours got their own palette slots). Removed.
- [x] Colour transfer: render = structure, colours = the photo's joint L/chroma distribution.
      In earthgen: `WorldSpec::color_profile` (data/profiles/mars.prof from
      tools/color_transfer.py --save-profile), applied to planet pixels before the palette,
      no vibrance on them; brightness half rank-matched, half mean/spread (never stretched).
      Paletted Mars vs paletted photo: L/a/b within ~2, sdL/sdC/hue_var ~1x (hue_var was 0.25x).
- [ ] Colour profiles per material class from several photos (rust, basalt, ice, tholin) so
      new planets get photo colours; blend by physics.
- Overnight loop (2026-11-03/04; sheets renders/loop/iterNN.png = photo | face 0 | yaw 160,
  colour-transferred; tools/loop_sheet.sh):
  - [x] it1: small craters too crisp/lunar, dark regions blurry -> mantle on young craters,
        rims ~ D^0.35, less wind-field blur, dust_soft 0.07.
  - [x] it2: limb darkened (rank transfer turned it basalt-grey), frost a band -> mosaic
        light also flattens day-side lighting; dust g 0.2 + 3x haze (stand-in for multiple
        scattering; limb stays bright to 0.9 R like the photo); frost solid only inside the
        cap, spots on pole-facing walls/hollows toward its edge.
  - [x] it3: no dark belts, smooth blob edges -> lift threshold halfway global/per band;
        wind + terrain on one scale (rank z-score); robust (16-84 %) scale for the lows
        (sd was dominated by canyons/basins: ordinary lows were ~0.05 sd, the terrain
        term did nothing).
  - [x] it4: crumpled-paper ground, uniform bright areas -> ground_detail 0.5, two-way
        wind streaks (bright tails in dark ground, dark tails in dusty ground). Detail
        stats at 128 px: band1 1.4x mars.png from the dust/terrain edge intricacy (terrain
        weight 0.15 compromise: 0 gives 1.2x but loses the edges seen at 800 px).
  - [x] it5: dark areas medium grey (relief outweighed albedo: the darkest ranks went to
        crater shadows) -> shading 1.0, dark_level 0.8.
  - [x] it6: colour profile moved into earthgen (see above); rank-only brightness stretched
        a dusty face's relief to the photo's range -> half rank / half capped affine.
  - [x] it7-8: lavender streaks (dark tails 0.7 -> 0.35); 1-px outline at 128 px (profile
        skipped the edge ring); craters within 3 default cells fade (anti-aliasing, in km).
  - [x] it9: high latitudes scoured bare (polar eddies strongest) while real Mars is bright
        there -> seasonal frost locks the ground (no lift within ~15 deg of the cap edge,
        ragged edge): bright high latitudes, dark mid-latitude belts.
  - [x] it10 (user feedback 2026-11-04): grey cast all over = the photo's washed-out limb in
        the profile (0.7-0.9 R loses ~4 chroma in every band) -> profile statistics from
        < 0.7 R; ice/frost (chroma/L < ~0.3) keeps its modelled white and stays out of the
        statistics; glow shell thin (`optics.rim` 0.3, knob air_rim) + haze 3 -> 1.5;
        frost to ~10 deg past the polar-night circle (55 deg, sheet pick B); dark_amount
        0.55 -> 0.45; crater_relief 0.7 -> 0.45; crater-floor sand x0.4.
  - Open: dark belts at mid latitudes, not the photo's low-latitude band; 128 px default face 1.4x fine detail (dust/terrain edge
    pattern); old
    valley networks; physical lighting that still shows relief.

### Physical colours (2026-11-04; user: no colour-line constraint, physics not hand-picked colours)
- [x] Icy worlds: material colours from clean ice + absorber column (tholin / radiolysed sulfur
      salts; fallout, trailing-side plasma dose, brine). Enhanced Pluto photo dropped.
- [x] First tries replaced the region shapes too (flat blobs, then speckle noise): user preferred
      the old shapes -> hybrid (user pick): old regions, physical colours
      (renders/variants/icy_hybrid.png). Europa red-brown 30 vs 30 %; Pluto dark 36 vs 18 % (old belt).
- [ ] Shapes, one change at a time: Europa's red concentrated in the middle with blue-grey limb;
      Pluto one dark region instead of a belt.
- [ ] Same approach for Mars (replace the photo profile with physical dust/basalt/frost
      materials once they match), Mercury, Titan's surface.
- [ ] Brightness: icy disks dimmer than the photos (lighting law).
- [x] Crater relief too deep everywhere (user: Mercury) -> one default `crater_relief` 0.4 for all
      worlds (was 1; Mars override 0.45 removed; ice x0.85 for relaxation).
      Sheet renders/variants/crater_relief.png (0.15-1.0 x Mercury/Mars/Europa/Pluto): 0.4 kept (user moved on).

### Phase D: solar system one by one (each adds one capability)
- [ ] Mercury (airless, craters, rays) -> Mars (thin dusty air, rust, caps, relict
      rivers) -> Venus (opaque cloud deck) -> Europa (ice shell, crack lines, tinted
      salts) -> Titan (orange haze) -> Pluto (N2/CH4 ice plains, tholin red).
      Comparison sheet: preset render next to its reference.

### Phase E: new planets
- [x] Sample inputs from plausible distributions -> derive -> render; sheet of random
      planets; reject or flag impossible combinations (2026-11-04): `random_inputs(seed)`
      (physics.cpp), `--random N`, `tools/random_sheet.py` -> renders/random_sheet.png.
      Insolation log-uniform (40% of planets in the habitable zone), ice-rich beyond the
      snow line, 25% moons of giants (Roche / Hill limits); 200 seeds: ~60% airless,
      ~10% ocean worlds, ~5% runaway. Physics fixes it exposed (presets unchanged):
      rust only with an early ocean or water vapour (oxidants), dust only on bare
      ground (ice < 90%), global ice has no edge.
- [ ] Open (seen on the first sheet): every rusty world gets the Mars photo profile ->
      Mars clones (physical rust/basalt/frost colours, as for the icy worlds); every
      N2-ice world gets one big Sputnik-like basin (big white blob); plain icy worlds
      without tholins/salts are featureless; M/K-dwarf light makes ice orange (physical,
      but no white balance).
- [x] History, not only equilibrium (2026-11-05; user: young Earth was molten, Mars was wet):
      magma oceans after formation or a giant impact (`impact_myr_ago`; lasts ~1 Myr dry,
      ~4 Myr with Earth's water, ~30x under steam inside the runaway limit), their heat in
      the climate (steam atmospheres -> white water-cloud deck); lava drawn from each cell's
      temperature (crust-plate borders + lows by area rank, Planck glow vs the starlight;
      also scorching day sides); snowball climates (`snowball`: frozen start, CO2 stays in
      carbonates, kept only where stable; plates continue under the ice). Sampler: 15%
      young, 10% recent impacts, 6% scorching orbits, 20% snowball starts, bone-dry rocky
      worlds allowed. 300 seeds: ~5% magma oceans, ~5% lava day sides, ~7% snowballs.
- [x] Richer molten worlds (2026-11-05; user: lava read as noise, wants clouds / shifting
      snow / blue ice interacting): lava crust driven by the weather flow (crust age
      advected, reset over upwelling plumes; exposed melt = youngest crust + a crack
      network); convective steam clouds over hot ground too thin for a deck, lit from
      below by the glow; young crust after the magma ocean closes (`lava_rift`: melt along
      its cracks for ~3 closing times); mineral frost from rock vapour on locked airless
      lava worlds; wind-drifted snow on icy rocky worlds with air (`snow_drift`), scoured
      areas = bare blue ice.
- [x] User picks (picker notes 2026-11-05): lava_39 all fine; lava_197 (nearly all molten)
      uniform/boring; molten types 1-9 fine, mineral frost boring (accepted: not every idea works).
- [x] Edge anomaly on glowing worlds (user): grey rim band + terminator crescent + dotted
      limb -> one glow/starlight exposure, glow skips the sun-path reddening, auto exposure
      as a per-ray gain (2026-11-06).
- [ ] Open: wet early Mars (needs extra early warming: H2-CO2 CIA / volcanic episodes);
      locked worlds' hot spot is fixed at lon 0, it doesn't follow `--sun-az`.
- [ ] Later: gas giants, rings, moons in frame, seasons/animations, binary stars.

## Preference learning (agreed 2026-11-07)

Goal (user): find planets the user likes that are also distinct from each other.
Judgments are comparisons (best of 2-4), not 0-9 scores. The user decides the DL
part (pretrained embedding such as DINOv2/CLIP, maybe label-free fine-tuning on the
pool, a preference head, diversity) once there is data, and wants to understand each step.

- [x] Pool of pre-rendered random planets (`tools/pool.py`, incremental, ~8 s each) +
      ranking app (`tools/rank.py`: rank-order sets of up to 6 (user: more efficient than
      best-of) -> `comparisons.jsonl`, Swiss-style set choice, Bradley-Terry ranking page) (2026-11-07).
- [x] Tier page (user: ranking within a set forced arbitrary calls between equally good/bad
      planets): batches of ~40, drag into tiers S-D; same tier = no preference (2026-11-07).
- [x] "+10 planets" into a batch in progress; black planet (#32, also #61/#72/#76 too dark):
      the glow exposure dimmed steam-deck worlds whose deck hides all the glow (2026-11-07).
- [x] Tier rows collapsed to one row (newest first, "all" expands), 96 px tiles (user: tall
      tiers needed scrolling); unsorted planets one at a time, large, keys 1-5 / space; click a tier to place it (2026-11-07).
- [x] Pool bugs (user): #67 frost over a whole hemisphere (retrograde obliquity not folded),
      #47 seams at cube faces/equator (rank ties ordered by cell index) (2026-11-07).
- [x] Variety (user: few molten/icy worlds seen, ~15% Mars clones). Census of 300 seeds: 43% ice,
      19% rusty, 11% ocean, 9% molten (10 of 17 magma oceans under steam decks). Ice worlds
      rendered plain (only plasma staining + haze fallout had colours): `weathering` = absorber
      column on old ground by exposure age (dark lag on old airless ice, Callisto; tholins from
      CH4 ice without haze), brine on planets/tholin worlds stains too. Rust: oxide by water
      history (`rust_ox` = ocean or vapour x age, `rust_wet` = liquid water now -> goethite
      ochre vs hematite red vs grey basalt); the Mars photo profile only for Mars. Presets and
      Earth byte-identical. Old pool + its judgments archived as `pool_v1/` (2026-11-07).
- [x] Balanced pool (user: demo types missing from the pool): `pool.py --balanced N` picks up
      to N seeds per world type from the --describe card (21 types; rare ones 1-4% each), the
      sampler unchanged; pool = seeds 1-100 + 91 balanced (2026-11-07).
- [ ] Open (optional, user's call): sampler weights (fewer frozen-over, more dry molten worlds);
      dusty worlds still use the Mars mosaic's relief light (relief_light=1), not physical light.
- [ ] Collect judgments (~200+), then the user picks the model design.
- [x] DINOv2 label-free fine-tuning (user, 2026-11-07): `tools/views.py` renders 4 raw 224 px
      views per planet (pool seeds first, then balanced by type; 576 planets in `views/`),
      `tools/dino.py` DINO-style fine-tune of ViT-S/14 (last 4 blocks, views of one planet =
      positives, Sinkhorn-Knopp centering: DINO's EMA centering collapsed onto ~10 slots), eval on
      91 held-out seeds: view@1 0.66 -> 0.89, hard@1 (view 0 vs 3) 0.40 -> 0.66, world-type 5-NN
      0.69 -> 0.65 (flat, noisy). Sheet `renders/dino/neighbors_vits14.png` (2026-11-08).
- [x] Pool = the 576 DINO-view planets (`pool.py pool --from views/index.json`, ~385 new renders),
      so every tier judgment has an embedding (2026-11-08).
- [x] Preference models (`tools/prefs.py`, 2026-11-08), on the 576-planet tiering, pairwise accuracy
      out-of-fold (0.5 chance): user's own re-tiering of 191 planets agrees 0.90 (noise ceiling);
      world type 0.76; raw generation inputs 0.70 (trees; can't learn the inputs -> world mapping
      from 576 planets); physics card 0.85 (trees > ridge); DINOv2 frozen 0.90 (ridge), fine-tuned
      0.88 (the fine-tune did not help); ensemble 0.89. Dislikes: Titan-like haze (mean tier 0.1),
      Pluto-like N2 ice; likes: old dark ice, living ocean worlds.
- [x] `tools/showcase.py` (user: N=42 interesting planets, A/B fine for variety): card trees skip
      likely-disliked seeds before rendering, ridge on frozen DINO embeddings of the pixel art
      (CV 0.90) predicts the tier, greedy MMR (B or better eligible, diversity 0.65) (2026-11-08).
- [ ] Open (user's call): re-weight the random sampler with the card model; tier the showcase.

### Showcase media + low orbit on new worlds (2026-11-08)
- [x] Spinning clips (`--spin`, `--sky-speed`; tools/spin.py: 10 most distinct showcase picks,
      0.3 turns / 8 s, sky 0.067x; user-picked speeds) -> renders/spin/.
- [x] LEO stills (tools/leo_showcase.py) + fly-along clips (renders/leo_anim/).
- [x] LEO streaks on non-Earth worlds: cause = the Earth-tuned sub-grid detail (noise relief
      scaled by |h|, normals x10; vegetation tint, lakes, rivers on every world). Now: noise
      relief tilt only on plate worlds, vegetation tint only with biomes, lakes/rivers only
      with an ocean; non-plate worlds render LEO on a 768 grid (real smaller craters, ~30 s).
- [ ] Open: lifeless plate worlds (#20177) keep the Earth relief streaks; LEO target on
      coastless worlds is a fixed point (lon 0; can be a featureless plain, #20097); dark
      glow exposure on #20080.
- [x] Tried + reverted (user, 2026-11-08: "the execution just was not good enough"): per-pixel
      small craters below the grid (hashed lattices, same population as craters.cpp) and grid
      craters at full depth on the LEO grid. Lower LEO sun (25 / 12 deg) didn't work either.
- [ ] Open: LEO detail on non-plate worlds (still soft: grid-scale colour patches).
