# Planet parameters (physics-based generation)

Mark `[x]` for what to include, leave `[ ]` to skip. Add notes after any line.

Each line has a tag:
- **in**: a natural *input* (rolled or chosen).
- **der**: usually *derived* from other parameters. It could still be overridden by hand.
- **viz**: what it changes in the image, so it's easy to see which ones pay off visually.

The fewer **in** parameters, the more "physics-based" the result: most of the
planet then follows from star, distance, mass, composition, age and spin.

**Suggested first set (marked `[x]`, 59 of ~140):** rocky / icy planets only (no gas
giants yet), seen as a still image. About 12 true inputs: star type, star activity,
orbital distance, planet mass, composition class, water/volatile fraction, age,
day length, axial tilt, moons, tectonic regime (as an override) and whether life /
civilisation exists. Everything else marked is derived from those. Picked because
they either change the picture a lot (atmosphere/haze, liquid, surface materials,
ice, pigment colour) or are needed to derive those. Left out for now: eccentricity,
multiple stars, rings, gas giants, detailed ocean currents/storm types, and the
time-varying events (seasons, eruptions...) — good later additions.

---

## 1. Star and planetary system

- [x] **Star mass / spectral type** (in): M dwarf ... F star. Sets luminosity, colour, lifetime and flare activity. viz: colour of sunlight
- [x] **Star luminosity** (der: star mass, star age). Sets insolation at a given distance.
- [x] **Star surface temperature / spectrum** (der: star mass). Sets the light colour, which wavelengths reach the ground, and plant pigment colour. viz: tint of the whole scene
- [ ] **Star age / evolutionary stage** (in): main sequence, subgiant, red giant, white dwarf. A brightening star drives a climate drift. viz: star size/colour in sky
- [x] **Star activity** (der: star mass, age): flares, UV/X-ray flux, stellar wind strength. Drives atmosphere escape, ozone and aurorae.
- [ ] **Metallicity of the system** (in): how much rock/metal vs gas formed. Sets the planet composition and the likelihood of rocky planets.
- [ ] **Multiple stars** (in): single, close binary (circumbinary), wide binary. viz: two suns, two shadows, double sunsets
- [x] **Orbital distance (semi-major axis)** (in). Sets insolation, the year length and the tidal-locking likelihood.
- [ ] **Orbital eccentricity** (in). Gives seasons from distance changes and a variable climate.
- [ ] **Orbital period / year length** (der: distance, star mass).
- [x] **Insolation / stellar flux** (der: luminosity, distance). The main climate driver.
- [ ] **Position relative to the habitable zone** (der: insolation). A quick summary label.
- [ ] **Neighbour giant planets** (in): shield or enhance the impact rate, perturb the orbit. viz: bright planets in the sky
- [ ] **Asteroid / comet belts** (in): impact rate, delivery of water and organics.
- [ ] **Location in galaxy / nebula backdrop** (in, render only). viz: star density, nebula in the background

## 2. Bulk planet

- [x] **Planet mass** (in). Sets gravity, atmosphere retention, interior heat and tectonics regime.
- [x] **Planet radius** (der: mass, composition, or in). Sets the size of features and the pixel scale.
- [x] **Bulk composition / class** (in): iron-rich (Mercury), rocky (Earth), water world, ice-rock (Ganymede), mini-Neptune, gas giant, ice giant.
- [x] **Core mass fraction** (der: composition). Sets density, magnetic dynamo potential and heat.
- [x] **Water / volatile mass fraction** (in). How much water, CO2, N2, CH4 and NH3 the planet received. Ranges from a dry rock to a 50% water ocean world.
- [ ] **Mean density** (der: mass, radius).
- [x] **Surface gravity** (der: mass, radius). Sets mountain height limits, atmosphere scale height, crater shapes and dune size.
- [x] **Escape velocity** (der: mass, radius). Decides which gases are kept.
- [x] **Planet age** (in). Drives the cooling of the interior (less volcanism with age), atmosphere loss, crater accumulation, erosion maturity and life evolution.
- [ ] **Formation history / migration** (in): formed beyond the snow line and migrated inward means water-rich.
- [ ] **Differentiation** (der: mass, early heat): layered core/mantle/crust vs undifferentiated.
- [ ] **Shape / oblateness** (der: rotation, gravity). viz: flattened fast-spinning planets

## 3. Rotation and orientation

- [x] **Rotation period / day length** (in). Sets the number of circulation cells and jets, storm sizes, and day-night temperature swing.
- [x] **Tidal locking / spin-orbit resonance** (der: distance, star mass, age; or in). Gives a permanent day side and a terminator climate. viz: eyeball planets
- [x] **Axial tilt (obliquity)** (in). Sets seasonal strength, ice-cap size and whether the poles are hotter than the equator (tilt > 54°).
- [ ] **Obliquity stability** (der: large moon present). Without a large moon the tilt wanders and the climate is chaotic.
- [ ] **Precession / Milankovitch cycles** (der). Drive ice ages over long timescales.
- [ ] **Season (current orbital phase)** (in, render). viz: which hemisphere is snowy

## 4. Moons and rings

- [x] **Number and size of moons** (in). Tides, obliquity stability, eclipses. viz: moons in the frame, moon shadows on the disk
- [ ] **Moon distances / orbits** (in). Tidal strength, eclipse frequency.
- [x] **Tidal heating of the planet** (der: moons/star, eccentricity). Volcanism even on small, old bodies (Io).
- [ ] **Ocean tide amplitude** (der: moons, star). Wide tidal flats, coastal erosion. viz: tidal flats, mud colours on coasts
- [ ] **Rings** (in or der: a moon broken up inside the Roche limit): extent, density, composition (ice = bright, rock = dark). viz: ring + ring shadow on the planet
- [ ] **Planet is itself a moon of a giant** (in): tidal heating, eclipses by the giant, a giant hanging in the sky. viz: big
- [ ] **Recent captured asteroid / debris** (in). Small irregular moons, meteor rate.

## 5. Interior and geology

- [x] **Internal heat budget** (der: mass, age, radiogenic elements, tidal heating). The master driver of geology.
- [ ] **Radiogenic element abundance** (in: U, Th, K). More gives longer-lived heat.
- [ ] **Surface heat flow** (der: heat budget, size).
- [x] **Mantle convection / tectonic regime** (der: heat flow, water, gravity; or in): plate tectonics, stagnant lid (Mars, Venus), episodic overturn, heat-pipe (Io). Sets continent shapes, mountain belts and the resurfacing style.
- [ ] **Number of plates and plate speeds** (der: regime, heat). Already in the generator as random values.
- [ ] **Lithosphere / crust thickness** (der: heat flow, age). Sets max mountain height and how flexure looks.
- [ ] **Crust types** (der: tectonics, water): basaltic ocean crust, granitic continents (need water + subduction), anorthosite highlands. viz: rock colours
- [x] **Continental fraction / hypsometry** (der: tectonics, age, water). The bimodal Earth-like elevation curve vs a unimodal one.
- [x] **Max relief / mountain height** (der: gravity, crust strength). Low gravity gives Olympus Mons sized peaks.
- [x] **Volcanism rate** (der: heat, regime). viz: lava flows, fresh dark basalt, volcanic plumes
- [x] **Volcanism type** (der: composition, temperature): basaltic effusive, explosive, sulfur (Io), cryovolcanism (water/ammonia geysers, Enceladus).
- [ ] **Hotspots / mantle plumes** (der: heat). Island chains, shield volcanoes, large igneous provinces.
- [ ] **Rifts, faults, fracture patterns** (der: tectonics, tidal stress). viz: Europa-like cracks, Valles Marineris
- [x] **Impact cratering history** (der: age, belts, giant neighbours). Crater density on old surfaces. viz: cratered terrain
- [x] **Surface age / resurfacing rate** (der: volcanism, erosion, tectonics). Decides how many craters survive.
- [x] **Magnetic field / dynamo** (der: liquid conducting core, convection, rotation; or in). Protects the atmosphere from the stellar wind and gives aurorae.
- [ ] **Magnetic field strength / magnetosphere size** (der: dynamo). Aurora size and brightness.
- [ ] **Interior water cycle** (der: tectonics). Subduction recycles water and CO2 and stabilises climate.

## 6. Atmosphere

- [ ] **Initial volatile inventory** (der: formation, water fraction, impacts).
- [x] **Atmospheric escape** (der: gravity, temperature, star activity, magnetic field, age): Jeans escape, hydrodynamic escape, stellar-wind stripping. Decides what remains.
- [x] **Surface pressure** (der: inventory, escape, outgassing, or in). From airless to Venus-like 90 bar. Sets haze thickness, limb glow, temperature evening-out and boiling points. viz: big (we just saw this)
- [x] **Composition — major gases** (der/in): N2, O2, CO2, H2O, H2, He, CH4, NH3, Ar, SO2, H2S, CO, Ne.
- [ ] **Trace / reactive gases** (der): O3 (needs O2), photochemical products.
- [ ] **Mean molecular weight** (der: composition). Sets the scale height.
- [x] **Scale height** (der: temperature, gravity, molecular weight). Sets atmosphere thickness at the limb. viz: rim glow width
- [x] **Rayleigh scattering colour** (der: composition, pressure, star spectrum). Blue sky vs other tints. viz: limb/sky colour
- [x] **Aerosols / hazes** (der: chemistry, volcanism, dust storms): photochemical tholin haze (Titan orange), sulfuric acid (Venus pale yellow), dust (Mars butterscotch), smoke/ash. viz: very big
- [x] **Greenhouse strength** (der: CO2, H2O, CH4, pressure). Surface temperature above equilibrium.
- [ ] **Ozone / UV shield** (der: O2, star UV). Habitability of land surfaces.
- [ ] **Vertical structure** (der): lapse rate, tropopause height, stratosphere inversion. Decides cloud heights.
- [ ] **Outgassing rate** (der: volcanism). Replenishes the atmosphere.
- [ ] **Atmospheric chemistry state** (der: life, volcanism, star UV): oxidising vs reducing. Changes the colours of everything.

## 7. Temperature and climate

- [x] **Bond albedo** (der: clouds, ice, surface, haze). Reflected fraction.
- [x] **Equilibrium temperature** (der: insolation, albedo).
- [x] **Mean surface temperature** (der: equilibrium temperature + greenhouse + internal heat).
- [x] **Heat redistribution efficiency** (der: pressure, rotation, ocean). Day-night and equator-pole contrast.
- [ ] **Equator-to-pole gradient** (der: redistribution, tilt).
- [ ] **Day-night temperature swing** (der: rotation, pressure, tidal locking).
- [ ] **Seasonal amplitude** (der: tilt, eccentricity, ocean fraction).
- [x] **Circulation regime** (der: rotation rate, radius, temperature): number of Hadley/Ferrel cells, jets and bands. Already Earth-tabled in climate.cpp. viz: cloud band pattern
- [ ] **Typical wind speeds** (der: heat flux, pressure, rotation). Drives cloud motion and dust.
- [ ] **Storm types and frequency** (der: rotation, latent heat, temperature contrast): cyclones, hurricanes, dust storms, polar vortices, giant long-lived vortices (Great Red Spot).
- [x] **Precipitation amount and type** (der: liquid, temperature): water rain, snow, methane rain, sulfuric acid virga, CO2 snow, "iron rain" on lava worlds.
- [x] **Ice caps and glaciation extent** (der: temperature, tilt, water). Includes the snowball-state possibility. viz: big
- [ ] **Permafrost / frost line** (der: temperature).
- [ ] **Climate stability feedbacks** (der): ice-albedo runaway, runaway / moist greenhouse, carbonate-silicate thermostat (needs tectonics + liquid water).
- [ ] **Climate history** (der: age, star brightening): past wetter/warmer phases leave dry riverbeds, old shorelines, salt flats. viz: Mars-like relict valleys
- [ ] **Lightning** (der: convection, cloud type). viz: night-side flashes in animations
- [ ] **Aurorae** (der: magnetic field, stellar wind, atmosphere). viz: polar glow on the night side

## 8. Liquids (hydrosphere and analogues)

- [x] **Surface liquid type** (der: temperature, pressure, inventory): water, methane/ethane, ammonia-water, liquid CO2, sulfuric acid, molten rock (lava ocean), none. Use phase diagrams of each at the surface T and P.
- [x] **Liquid amount / ocean fraction / mean depth** (der: inventory, escape, hypsometry). Sea level already exists as a percentile.
- [ ] **Ocean salinity / dissolved chemistry** (der: age, weathering). Changes colour, freezing point and evaporites.
- [x] **Ocean colour** (der: liquid type, depth, life, sediments, sky). viz: big
- [ ] **Ocean currents and gyres** (der: rotation, winds, continents). Heat transport (our missing Gulf Stream).
- [x] **Sea ice / frozen oceans** (der: temperature, salinity). viz: Europa-like ice shells
- [ ] **Subsurface oceans** (der: tidal heat, ice shell). Feed geysers / cryovolcanism.
- [x] **Rivers, lakes, deltas** (der: precipitation, relief). Already simulated.
- [ ] **Erosion rate and style** (der: liquid, precipitation, wind, gravity): fluvial, glacial, aeolian, chemical. Erosion in the generator is fluvial only.
- [ ] **Groundwater / wetlands** (der: rain, relief). viz: green or dark lowlands
- [ ] **Supercritical fluid layers** (der: high pressure/temperature). There is no surface: "oceans" fade into the atmosphere.

## 9. Surface solids and materials (most of the colour comes from here)

- [x] **Bedrock types** (der: crust, volcanism): basalt (dark grey), granite (pale pink-grey), anorthosite (bright), sedimentary (layered tan).
- [x] **Weathering products** (der: water, O2, temperature): iron oxides (rust red/orange), clays, carbonates (white), laterites.
- [x] **Evaporites / salt flats** (der: dried seas, arid climate). viz: bright white basins
- [x] **Sand, dunes, dust** (der: wind, aridity, gravity). viz: dune seas, dust-coloured plains
- [ ] **Regolith maturity / space weathering** (der: atmosphere thickness, magnetic field, age). Airless bodies darken over time.
- [x] **Ices** (der: temperature, pressure, inventory): water ice, CO2 frost (seasonal caps), N2/CH4/CO ices (Pluto), ammonia ice. viz: white/pink/blue tints
- [x] **Sulfur and sulfur compounds** (der: volcanism): yellow, orange, red, black (Io).
- [x] **Organics / tholins** (der: CH4 + UV): brown-orange-red (Titan, Pluto). viz: big
- [x] **Fresh lava / glowing rock** (der: volcanism, surface temperature). viz: emissive night-side glow; reuses city-light machinery
- [ ] **Impact ejecta / ray systems** (der: craters). Bright rays on young craters.
- [ ] **Glass / obsidian / impact melt** (der: impacts, volcanism).
- [ ] **Surface roughness / texture** (der: geology). Changes how relief shading looks.

## 10. Life and biosphere

- [x] **Life present** (der: liquid water, time, energy; or in).
- [ ] **Time since life began / evolutionary stage** (der: age): microbial, oxygenated atmosphere, complex land life.
- [x] **Biomass coverage / productivity** (der: climate, light, nutrients). Already the biome mapping.
- [x] **Photosynthetic pigment colour** (der: star spectrum): green (Sun), red/orange (F stars), black/purple (M dwarfs). viz: very big for land colour
- [ ] **Marine life / algal blooms** (der: nutrients, light). viz: ocean colour patches
- [ ] **Biological atmosphere changes** (der: life): O2, CH4, and the knock-on ozone and rust colours.
- [ ] **Biological soil and rock colours** (der: life): microbial mats, lichens, dark soils.
- [x] **Intelligent life / civilisation** (in): population, tech level. Cities already exist.
- [ ] **City light colour** (der: tech, star): sodium orange vs LED white vs alien.
- [ ] **Artificial structures** (in): megastructures, orbital rings, satellites, space elevators, terraforming. viz: optional fun
- [ ] **Industrial effects** (der: civilisation): pollution haze, deforestation, warming.

## 11. Giant-planet specific (if gas/ice giants are included)

- [ ] **Envelope composition** (der: formation): H2/He fraction, heavy-element enrichment.
- [ ] **Internal heat flux vs absorbed sunlight** (der: mass, age). Jupiter emits about 2x what it absorbs, which drives its storms.
- [ ] **Cloud deck chemistry by depth** (der: temperature profile): NH3 ice, NH4SH, H2O; deeper/hotter gives silicate, iron clouds.
- [ ] **Chromophores** (der: chemistry, UV): the reds, browns and oranges of bands.
- [ ] **Number and width of jets/bands** (der: rotation, radius). The barotropic weather sim does this.
- [ ] **Long-lived vortices** (der: jets, heat). viz: Great Red Spot
- [ ] **Methane absorption** (der: CH4 amount, depth). Uranus/Neptune blue-cyan colour.
- [ ] **Polar features** (der: rotation, jets). Hexagons, polar cyclone clusters.

## 12. Time-varying events (mostly for animations)

- [ ] **Seasonal changes** (der: tilt, orbit): growing and shrinking caps, vegetation and frost.
- [ ] **Dust storms / global dust events** (der: wind, dust).
- [ ] **Volcanic eruptions and plumes** (der: volcanism).
- [ ] **Geysers / cryovolcanic plumes** (der: subsurface ocean).
- [ ] **Aurora flicker, lightning** (der: magnetic field, storms).
- [ ] **Impacts / meteors** (der: impact rate).
- [ ] **Eclipses and moon shadows** (der: moons).
- [ ] **Ring shadow movement** (der: rings, season).

## 13. Render / observation (not physics, but part of the scene)

- [ ] **Viewing distance and framing** (in): disk, LEO, far view with moons.
- [ ] **Phase angle / sun direction** (in): full, half, crescent, night side.
- [ ] **Exposure and palette size** (in).
- [ ] **What is visible in the background** (der: system): the star itself, sibling planets, moons, nebula.
