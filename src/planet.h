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
    explicit Planet(int n) : grid(n) {}

    // Terrain
    std::vector<float> elevation;     // metres above sea level
    std::vector<int> region, plate;   // building blocks (-1 when loaded from data)
    std::vector<uint8_t> continental; // crust type (1 = continental)
    std::vector<Vec3> plate_velocity; // per-cell surface velocity (tangent)
    std::vector<float> convergence;   // >0 converging boundary, <0 diverging (0 inside plates)

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
