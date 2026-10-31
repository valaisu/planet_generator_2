// Cities: placed where Earth puts them, not copied from Earth.
//
// Habitability per land cell from the simulated fields: mild temperature, enough
// rain (or a big river through dry land — the Nile), low and flat ground, close
// to the coast or a river; zero on ice. A low-frequency "settlement history"
// factor clusters it into dense regions and empty ones (Europe/India vs Canada).
// Cities are drawn with probability ~ habitability^2 (people concentrate), sizes
// from Zipf's law (a few huge metros, many towns). The renderer lights them on
// the night side; `city_cells` indexes them by grid cell for that lookup.
#include <algorithm>
#include <cmath>
#include <random>

#include "noise.h"
#include "planet.h"

namespace {
float smooth01(float a, float b, float x) {
    float t = std::clamp((x - a) / (b - a), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}
}  // namespace

float city_radius_km(float size) { return 1.5f * std::sqrt(size); }

void place_cities(Planet& P, const PlanetParams& prm) {
    const CubeGrid& g = P.grid;
    const int N = g.size();
    const float KM = EARTH_RADIUS_KM;
    std::mt19937 rng(prm.seed * 7919u + 13u);
    std::uniform_real_distribution<float> U(0.0f, 1.0f);
    const Vec3 off(U(rng) * 60.0f, U(rng) * 60.0f, U(rng) * 60.0f);

    std::vector<int> sea;
    std::vector<float> land_h(N);
    for (int c = 0; c < N; ++c) {
        land_h[c] = std::max(P.elevation[c], 0.0f);
        if (P.elevation[c] <= 0.0f) sea.push_back(c);
    }
    std::vector<float> d_sea = distance_from(g, sea);

    P.habitability.assign(N, 0.0f);
    std::vector<double> cdf(N);
    double total = 0.0;
    for (int c = 0; c < N; ++c) {
        float h = P.elevation[c], t = P.temperature[c], p = P.precip[c];
        float hab = 0.0f;
        if (h > 0.0f) {
            float river = P.drainage.empty() ? 0.0f : smooth01(1.5e4f, 3e5f, P.drainage[c]);
            float f_t = smooth01(-3.0f, 9.0f, t) * (1.0f - 0.35f * smooth01(25.0f, 29.0f, t));
            float f_p = std::max(smooth01(200.0f, 650.0f, p), river) * (1.0f - 0.7f * smooth01(1700.0f, 2600.0f, p));  // rainforest
            float f_h = smooth01(2800.0f, 300.0f, h);
            float slope = length(g.gradient(land_h, c)) / (KM * 1000.0f);
            float f_s = 1.0f - 0.8f * smooth01(0.004f, 0.02f, slope);
            float water = 1.0f + 1.5f * std::exp(-d_sea[c] * KM / 120.0f) + 1.2f * river;
            float history = 0.5f + 1.0f * smooth01(0.3f, 0.7f, fbm3(g.dir(c) * 2.2f + off, 3));
            hab = f_t * f_p * f_h * f_s * water * history;
        }
        P.habitability[c] = hab;
        total += static_cast<double>(hab) * hab * g.area(c);
        cdf[c] = total;
    }
    P.cities.clear();
    if (total <= 0.0) return;

    // Earth: ~4000 cities over 100k people, ~12000 towns over 30k; size 1 =
    // 30k, Zipf (Pareto a = 1), capped at 300 (~10M).
    constexpr int NCITIES = 12000;
    for (int i = 0; i < NCITIES; ++i) {
        double r = U(rng) * total;
        int c = static_cast<int>(std::lower_bound(cdf.begin(), cdf.end(), r) - cdf.begin());
        c = std::min(c, N - 1);
        Vec3 e, n;
        CubeGrid::tangent_basis(g.dir(c), e, n);
        float cell = std::sqrt(g.area(c) * 4.0f * 3.14159265f);  // cell width, radians
        Vec3 d = normalize(g.dir(c) + (e * (U(rng) - 0.5f) + n * (U(rng) - 0.5f)) * cell);
        if (g.sample(P.elevation, d) <= 0.0f) d = g.dir(c);
        float size = std::min(300.0f, 1.0f / std::max(1e-3f, U(rng)));
        P.cities.push_back({d, size});
    }

    // Index: every cell within reach of a city's light (incl. the blur a coarse
    // view applies, up to CITY_REACH_KM) lists it.
    std::vector<std::vector<int>> lists(N);
    for (int i = 0; i < static_cast<int>(P.cities.size()); ++i) {
        const City& ct = P.cities[i];
        float reach = std::max(2.5f * city_radius_km(ct.size), CITY_REACH_KM) / KM;
        int c0 = g.cell_of(ct.dir);
        std::vector<int> stack{c0}, seen{c0};
        while (!stack.empty()) {
            int c = stack.back();
            stack.pop_back();
            lists[c].push_back(i);
            for (int nb : g.nbr(c)) {
                if (std::find(seen.begin(), seen.end(), nb) != seen.end()) continue;
                seen.push_back(nb);
                // Within reach plus half a cell (the cell's extent).
                if (std::acos(std::clamp(dot(g.dir(nb), ct.dir), -1.0f, 1.0f)) < reach + 0.004f) stack.push_back(nb);
            }
        }
    }
    P.city_start.assign(N + 1, 0);
    P.city_index.clear();
    for (int c = 0; c < N; ++c) {
        P.city_start[c] = static_cast<int>(P.city_index.size());
        P.city_index.insert(P.city_index.end(), lists[c].begin(), lists[c].end());
    }
    P.city_start[N] = static_cast<int>(P.city_index.size());
}
