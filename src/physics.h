#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "planet.h"

// Physics-based planets: a few physical inputs -> derive() -> everything else,
// in dependency order (see PLAN.md "Physics-based planets"). Numbers only; the
// generator pipeline will consume PlanetPhysics in phase C.
// Units: SI unless the name says otherwise (bar, K, Gyr, Earth = 1 units).

enum class Composition { IronRich, Rocky, IceRock };
enum class Regime { Auto, PlateTectonics, StagnantLid, Dead, HeatPipe, IceShellActive, IceShellInactive };
enum class Tristate { Auto, No, Yes };

struct PhysicalInputs {
    std::string name = "custom";
    // Star (age = planet age)
    double star_mass = 1.0;           // solar masses
    double orbit_au = 1.0;            // planet (or host planet) distance from the star
    // Bulk
    double mass = 1.0;                // Earth masses
    Composition composition = Composition::Rocky;
    double core_fraction = -1.0;      // iron core mass fraction; < 0 = from composition
    double volatile_fraction = 2.3e-4;// water + other volatiles, mass fraction
    double age_gyr = 4.57;
    // Rotation
    double day_hours = 24.0;          // sidereal rotation; ignored when tidally locked to a host
    double obliquity_deg = 23.4;
    // Moons (count and the largest moon's mass / planet mass; visuals later)
    int moons = 1;
    double moon_mass_ratio = 0.0123;
    // When the body is itself a moon of a giant: the host and the forced orbit
    // (tidal heating, synchronous rotation). host_mass = 0: orbits the star.
    double host_mass_kg = 0.0, host_distance_km = 0.0, host_eccentricity = 0.0;
    // History: a giant impact this long ago melted the surface (< 0: none); the
    // climate started frozen over (snowball; kept only where that state is stable).
    double impact_myr_ago = -1.0;
    bool snowball = false;
    // Overrides / choices
    Regime regime = Regime::Auto;
    Tristate life = Tristate::Auto;
    bool civilisation = false;
};

struct Gas { std::string name; double bar; };
struct Volatile {                     // one condensable species at the surface
    std::string name;
    double inventory_bar = 0;         // retained inventory as a surface pressure
    double gas_bar = 0;               // partial pressure in the atmosphere
    std::string state;                // "gas", "liquid", "ice", "polar ice", "seasonal frost", ...
};

struct PlanetPhysics {
    // Star
    double star_lum = 1, star_teff = 5772, star_lifetime_gyr = 10;
    double star_rgb[3] = {1, 1, 1};
    double xuv_rel = 1;               // lifetime XUV fluence relative to Earth's
    double insolation = 1;            // Earth = 1
    // Bulk
    double core_fraction = 0.32, density = 5510, radius_km = 6371, gravity = 9.81, v_esc_kms = 11.2;
    // Rotation / tides
    double lock_time_gyr = 0, rotation_hours = 24;
    bool tidally_locked = false;
    double tidal_heat_wm2 = 0;
    // Interior
    double heat_flux_wm2 = 0.08;      // total surface heat flow
    double melt_myr = 1e9;            // since the surface was last molten (formation or a giant impact)
    double magma_tau_myr = 0;         // how long that magma ocean lasts
    double melt = 0;                  // 0..1: share of the surface still molten (lava between crust plates)
    double rift = 0;                  // after the crust closed: molten share still breaking out along its cracks
    Regime regime = Regime::PlateTectonics;
    double volcanism = 1;             // Earth = 1
    std::string volcanism_type;
    bool dynamo = true; std::string field;
    double surface_age_gyr = 0.1, crater_density = 0;  // craters >1 km, relative to lunar maria
    double max_relief_km = 20, continental_fraction = 0.4;
    std::string hypsometry;
    double ice_shell_km = 0;          // icy bodies: conductive shell over a subsurface ocean
    // Atmosphere
    double escape_ratio = 1;          // XUV / v_esc^4 vs the cosmic shoreline (Earth 1)
    double pressure_bar = 1, mu = 28.97, scale_height_km = 8.5;
    std::vector<Gas> gases;
    std::vector<Volatile> volatiles;
    double rayleigh_tau[3] = {0, 0, 0};  // optical depth at 650 / 550 / 450 nm
    std::vector<std::string> aerosols, cloud_types;
    double cloud_amount = 0;          // mean cloud opacity for the renderer
    double greenhouse_tau = 0;
    bool runaway = false;
    bool snowball = false;            // ocean frozen over (CO2 still in carbonates)
    // Climate
    double albedo = 0.3, t_eq = 255, t_mean = 288, t_equator = 300, t_pole = 260, t_winter_pole = 230;
    double redistribution = 0.2;      // 0 = radiative equilibrium per latitude, 1 = uniform
    int cells_per_hemisphere = 3;
    double hadley_deg = 30;           // Hadley cell edge latitude (90 = one global cell)
    std::string circulation;
    // Liquids / ice
    std::string liquid = "none";
    double ocean_depth_m = 0, ocean_fraction = 0, ice_fraction = 0;
    std::string ocean_colour;
    // Surface
    std::vector<std::string> materials;
    // Iron oxides (rusty worlds): degree of oxidation (0 = grey basalt, 1 = fully
    // rusted) and hydration (0 = red hematite, dry; 1 = ochre goethite, wet).
    double rust_ox = 0, rust_wet = 0;
    // Life
    bool life = false, oxygen = false;
    double pigment_rgb[3] = {0, 0, 0};
};

PlanetPhysics derive(const PhysicalInputs& in);
void describe(const PhysicalInputs& in, const PlanetPhysics& p);  // the "planet card"

// What the generator stages build from it (terrain style, ocean, craters, air
// optics, clouds, materials). Also sets prm.land_fraction / cloud_amount.
WorldSpec world_from_physics(const PhysicalInputs& in, const PlanetPhysics& p, PlanetParams& prm);

// Solar-system presets with real inputs; name -> false if unknown.
bool planet_preset(const std::string& name, PhysicalInputs& in);
std::vector<std::string> preset_names();
// A new planet: plausible random inputs (same seed -> same planet).
PhysicalInputs random_inputs(uint32_t seed);
// Derived vs real values for every preset (stdout table).
void check_presets();
