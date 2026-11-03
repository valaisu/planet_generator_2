#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "grid.h"

// Knobs for one generated world. Physical units: metres, km, degrees C.
struct PlanetParams {
    uint32_t seed = 1;
    int grid_n = 256;            // cells per cube-face edge
    float land_fraction = 0.29f; // Earth: 0.29
    int regions = 300;           // warped Voronoi regions (continent/plate building blocks)
    int continents = 5;
    int plates = 14;
    float cloud_amount = 0.24f;  // mean cloud opacity (Earth ~0.24, NASA cloud map)
};

constexpr float EARTH_RADIUS_KM = 6371.0f;

// What kind of world the stages build: filled from the physics (world_from_physics
// in physics.h); the defaults are today's Earth, so a plain run is unchanged.
// Elevations are Earth-equivalent metres (real height x R_earth / R): the
// renderer shows every planet at the same size, so relief reads as h / R.
enum class TerrainStyle { Plates, Stagnant, Dead, IceShell };

// Atmosphere optics for the renderer (Earth = 1 / white).
struct AtmoOptics {
    bool enabled = true;
    float rayleigh = 1.0f;          // x Earth's Rayleigh coefficients (spectrum stays lambda^-4)
    float mie = 1.0f;               // x Earth's aerosol coefficient
    Vec3 mie_albedo{1, 1, 1};       // aerosol single-scattering colour (dust, tholin)
    float height = 1.0f;            // scale height / radius, x Earth's
    float mie_g = 0.76f;            // aerosol phase asymmetry (forward scattering)
    float mie_height = 1.0f;        // aerosol scale height, x Earth's (well-mixed dust: 2 = the gas's)
};

struct WorldSpec {
    // Terrain
    TerrainStyle style = TerrainStyle::Plates;
    bool ocean = true;              // false: no liquid at all (no sea level)
    float relief = 1.0f;            // relief amplitude for non-plate styles (Earth-equivalent, x Earth)
    float volcanism = 1.0f;         // x Earth: number of shield volcanoes (stagnant lid)
    float canyons = 0.0f;           // radial canyon system + grabens around a volcanic rise (depth x)
    bool cracks = false;            // tidally flexed ice shell: lineae
    float lineae = 1.0f, lineae_width = 1.0f;  // their number / width, x (>= ~a grid cell or they dot out)
    bool glacier_basin = false;     // volatile (N2) ice filling a big young basin
    float crater_density = 0.0f;    // craters > 1 km relative to lunar maria on old crust (saturates ~10)
    float crater_density_young = 0.0f;  // ... on fully resurfaced plains
    float radius_km = EARTH_RADIUS_KM;  // real size (crater sizes / depths)
    float crater_relief = 1.0f;     // crater depth factor (ice relaxes / frost mantles them)
    float ground_detail = 0.0f;     // stagnant lid: knobby old crust, boundary mesas, wrinkle ridges, x
    float crater_fill = 0.0f;       // old craters filled (flat floors, worn rims) by air/water, x age
    float ejecta_lobes = 0.0f;      // volatile-rich ground: lobate (rampart) ejecta, central pits
    float fresh = 0.03f;            // youngest share of craters still bright (halo; rays if > 2 cells)
    bool erosion = true;
    // Climate
    float t_offset = 0.0f;          // C added to Earth's latitude curve (ocean worlds)
    float t_equator = 27.0f, t_pole = -20.0f;  // C, dry worlds (locked: day side, night side)
    bool locked = false;            // tidally locked to the star; substellar point at lon 0
    float jet_lat = 45.0f;          // main jet of the weather sim (deg); a weaker one 20 deg poleward
    // Light and air
    Vec3 star_color{1, 1, 1};       // sunlight tint (white-balanced to the Sun)
    AtmoOptics optics;
    // Clouds: deck = an opaque global layer (Venus, Titan) shaded from deck_dark
    // (thin weather) to deck_light (thick); otherwise ordinary clouds.
    bool deck = false;
    Vec3 cloud_color{0.90f, 0.94f, 1.0f};
    Vec3 deck_dark{0.8f, 0.7f, 0.5f}, deck_light{0.9f, 0.9f, 0.85f};
    float deck_asym = 0.0f;         // hemispheric shading of the deck (seasons)
    float deck_texture = 1.0f;      // swirl contrast: convective clouds 1, photochemical haze ~0.2
    float lommel_seeliger = 0.0f;   // ground scattering law: 0 Lambert, 1 Lommel-Seeliger (dark regolith)
    float saturation = 1.0f;        // material colour saturation (lifeless worlds), x
    float edge = 1.0f;              // width of the transitions between material regions, x
    float grain = 1.0f;             // gentle brightness noise everywhere (+-6% at 1), x
    // Surface colour: Earth biomes, or the material model below.
    bool biomes = true;
    Vec3 rock{0.4f, 0.4f, 0.4f};    // base surface material
    Vec3 dark{0.3f, 0.3f, 0.3f};    // dark patches / old basalt
    float dark_amount = 0.3f;       // share of the surface in dark patches
    float dark_mix = 0.7f, dark_level = 1.0f;  // how fully / how dark (x `dark`) they show
    // A third material with a different hue/saturation (fine dust, grey-green ice,
    // CH4 frost...), mixed over soft regions at the *current brightness*: real
    // surfaces show several colour families at equal brightness, not one ramp.
    Vec3 alt{0.5f, 0.5f, 0.5f};
    float alt_amount = 0.0f;
    float exposure = 1.0f;          // sunlight scale (1 = Earth's calibrated exposure)
    float auto_p95 = 0.0f;          // > 0: like a camera, scale the lit planet so its 95th-percentile luma = this
    Vec3 bright{0.8f, 0.8f, 0.8f};  // fresh crater ejecta and rays
    Vec3 ice{0.9f, 0.9f, 0.9f};     // polar caps / volatile ice
    float ice_below = -1000.0f;     // C: surface ice where colder (polar caps)
    // Seasonal frost (condensable air, e.g. CO2 on Mars): the polar night of the
    // winter hemisphere (south here) cools to the frost point and frosts over,
    // poleward of `frost_lat` (deg; 90 = none).
    float frost_lat = 90.0f;
    Vec3 frost{0.92f, 0.90f, 0.88f};
    Vec3 stain{0.5f, 0.3f, 0.2f};   // cracks + chaos (Europa salts) or tholins (Pluto)
    float stain_amount = 0.0f;
    // Albedo coupled to the terrain (> 0 brighter, < 0 darker), by process:
    // airless: steep slopes stay fresh (bright) while flats space-weather dark;
    // dusty air: dust settles on flats, slopes/crests show dark rock;
    // icy: frost collects on cold crests.
    float slope_albedo = 0.0f, crest_albedo = 0.0f;
    float mottle = 0.0f;            // small-scale albedo noise (1-3 px); 0: it reads as noise
    float roughness = 0.0f;         // grid-scale ridged relief noise; 0: same reason
    // Discrete small features (counts x these; 0 = none):
    float scarps = 0.0f;            // lobate scarps (old crust) + wrinkle ridges (young plains)
    float streaks = 0.0f;           // wind streaks downwind of craters (dusty air)
    float scarp_height = 1.0f, streak_strength = 1.0f;  // their visual strength
    float shading = 1.0f;           // relief-shading exaggeration, x the default
    // Relief light: 0 = physical (the true sun), 1 = mosaic-style (each patch lit
    // by its own sun `relief_sun` degrees above the horizon, from the screen's
    // upper left, as in mapped mosaics: relief reads even on a full disk).
    float relief_light = 0.0f, relief_sun = 35.0f;
    // Dusty air (Mars): bright dust settles where the surface wind is too weak to
    // lift it, dark rock shows where it is scoured (the weather sim decides,
    // clouds.cpp); lift falls with elevation (thinner air, `dust_elev`).
    float dust = 0.0f;              // 0 = off, 1 = on
    float dust_elev = 1.0f;         // weight of the air density in the lift
    float sand_lows = 0.0f;         // dark sand in hollows / dust on rims, x
    // Wind-blown dust from the simulated weather (clouds.cpp): wind lifts dust where
    // it is strong, the eddies carry it, it settles where the air is calm; the
    // ground keeps what `dust_steps` weather steps left (more = more storms laid
    // over each other). lift = windiest share of the ground lifting at a time.
    float dust_steps = 400.0f, dust_lift = 0.25f;
    // Fine dust texture: a cascade of `dust_octaves` (4 = down to ~2 disk px) at
    // coordinates advected by the wind over the last `dust_window` steps
    // (longer = more stretched), mixed in by `dust_texture`.
    float dust_octaves = 4.0f, dust_window = 75.0f, dust_texture = 0.4f;
    float dust_soft = 0.15f;         // dust cover: transition half-width in area rank (0.08 ~ two materials)
    bool lights = true;             // civilisation: cities + night lights
};

// Every simulated field lives on the same cube-sphere grid.
// A city: where it is and how big (1 = ~30k people, Zipf-distributed).
struct City {
    Vec3 dir;
    float size;
};
float city_radius_km(float size);           // lit core radius
constexpr float CITY_REACH_KM = 150.0f;     // lookup reach (covers coarse-view blur)

struct Planet {
    CubeGrid grid;
    WorldSpec world;
    explicit Planet(int n) : grid(n) {}

    // Terrain
    std::vector<float> elevation;     // metres above sea level
    std::vector<int> region, plate;   // building blocks (-1 when loaded from data)
    std::vector<uint8_t> continental; // crust type (1 = continental)
    std::vector<Vec3> plate_velocity; // per-cell surface velocity (tangent)
    std::vector<float> convergence;   // >0 converging boundary, <0 diverging (0 inside plates)
    // Non-plate styles (empty on Earth-like worlds)
    std::vector<float> resurfaced;    // 0..1: young plains (fewer craters; volatile ice in basins)
    std::vector<float> crack;         // 0..1: ice-shell lineae
    std::vector<float> ray;           // 0..1: fresh crater ejecta / rays (bright)
    std::vector<float> streak;        // 0..1: wind streaks (dust tails behind craters)
    std::vector<float> crater_sand;   // 0..1: dark sand on crater floors (dusty air)
    std::vector<float> wind_dust;     // surface dust laid down by the simulated weather (dusty worlds)

    // Drainage (compute_drainage, after the climate)
    std::vector<int> flow_to;         // receiver cell of each land cell (-1 for sea)
    std::vector<float> drainage;      // upstream runoff, km^2 at 1000 mm/yr (river size)

    // Climate
    std::vector<float> temperature;   // annual mean, deg C
    std::vector<float> pressure;      // sea-level pressure, hPa
    std::vector<Vec3> wind;           // tangent vector
    std::vector<float> precip;        // relative annual precipitation (Earth-ish mm/yr)
    std::vector<float> humidity;      // relative humidity at steady state (0..1)
    std::vector<float> sst_anomaly;   // ocean-gyre temperature anomaly (cold currents < 0)
    std::vector<float> cloud;         // cloud opacity 0..1 (a snapshot of weather)
    std::vector<float> cloud_tau;     // optical thickness from the grid-resolved cascade
    Vec3 cloud_noise_off;             //   octaves; cloud_opacity() adds the finer ones
    bool clouds_animated = false;     //   (except in animations: no ground-fixed noise)
    std::vector<float> weather_vorticity;  // relative vorticity of the simulated weather
    std::vector<Vec3> cloud_wind;     // simulated weather wind (tangent, ~1 = jet speed)

    // Cities (place_cities)
    std::vector<float> habitability;  // relative, land only
    std::vector<City> cities;
    std::vector<int> city_start, city_index;  // per cell: cities whose light reaches it

    bool is_land(int c) const { return elevation[c] > 0.0f; }
};

// Stages, in pipeline order.
void generate_terrain(Planet& p, const PlanetParams& prm);
bool load_earth_terrain(Planet& p, const std::string& path);  // real Earth relief
bool load_cloud_image(Planet& p, const std::string& path);    // diagnostic: real clouds
void add_craters(Planet& p, const PlanetParams& prm);  // non-plate worlds, after the terrain
// Override WorldSpec knobs from "key=value,key=value" (for variant sheets); false
// + message on an unknown key.
bool set_world_knobs(WorldSpec& w, const std::string& spec);
void erode(Planet& p, const PlanetParams& prm);
void simulate_climate(Planet& p, const PlanetParams& prm);
void compute_drainage(Planet& p);  // rivers: flow_to + drainage (needs the climate)
void place_cities(Planet& p, const PlanetParams& prm);  // needs climate + drainage
void simulate_clouds(Planet& p, const PlanetParams& prm);
// Animations: keep the weather running. start_clouds = simulate_clouds that
// keeps the state; advance_clouds runs it on by `minutes` of weather and
// rewrites the cloud fields — clouds drift with the wind, grow, decay, morph.
struct CloudSim;
struct CloudSimDelete { void operator()(CloudSim* s) const; };  // CloudSim is opaque here
using CloudSimPtr = std::unique_ptr<CloudSim, CloudSimDelete>;
CloudSimPtr start_clouds(Planet& p, const PlanetParams& prm);
void advance_clouds(CloudSim& sim, Planet& p, float minutes);
// Render-time cloud opacity at a direction: the grid's thickness times the
// cascade octaves the grid can't hold, up to `octaves` in total (8 = the fitted
// cascade; more adds sub-grid detail for close views).
float cloud_opacity(const Planet& p, Vec3 d, int octaves);
void clear_clouds(Planet& p);  // cloudless worlds: empty cloud fields, no weather run
