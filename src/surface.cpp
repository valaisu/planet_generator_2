#include <algorithm>
#include <cmath>

#include "surface.h"

std::vector<Vec3> relief_normals(const Planet& P, float exaggeration) {
    const CubeGrid& g = P.grid;
    std::vector<Vec3> n(g.size());
    std::vector<float> land_h(g.size());
    for (int c = 0; c < g.size(); ++c) land_h[c] = std::max(P.elevation[c], 0.0f);
    #pragma omp parallel for schedule(static)
    for (int c = 0; c < g.size(); ++c) {
        Vec3 d = g.dir(c);
        if (P.elevation[c] <= 0.0f) { n[c] = d; continue; }
        Vec3 grad = g.gradient(land_h, c) / (EARTH_RADIUS_KM * 1000.0f);  // slope, m/m
        n[c] = normalize(d - grad * exaggeration);
    }
    return n;
}

namespace {
float smooth01(float a, float b, float x) {
    float t = std::clamp((x - a) / (b - a), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}
Vec3 mix(Vec3 a, Vec3 b, float t) { return a + (b - a) * std::clamp(t, 0.0f, 1.0f); }

struct Biome { float t, p; Vec3 color; };
// Annual mean T (C), precipitation (mm/yr), albedo. Started from photo samples
// (Amazon, US plains/forest, Borneo, Australia), then pushed apart/saturated:
// the photos already include haze, and land must read at 128 px.
const Biome BIOMES[] = {
    {-7, 250, {0.38f, 0.35f, 0.30f}},   // tundra
    {-2, 550, {0.10f, 0.18f, 0.09f}},   // boreal forest
    {4, 750, {0.12f, 0.21f, 0.10f}},    // boreal / mixed
    {6, 250, {0.48f, 0.42f, 0.27f}},    // cold steppe
    {11, 120, {0.55f, 0.48f, 0.38f}},   // cold desert
    {10, 950, {0.16f, 0.30f, 0.11f}},   // temperate forest
    {13, 480, {0.40f, 0.42f, 0.19f}},   // grassland
    {17, 380, {0.45f, 0.37f, 0.22f}},   // mediterranean shrub
    {18, 1500, {0.13f, 0.27f, 0.09f}},  // subtropical forest
    {24, 350, {0.53f, 0.44f, 0.24f}},   // dry savanna
    {25, 900, {0.33f, 0.36f, 0.14f}},   // savanna
    {26, 2200, {0.08f, 0.23f, 0.06f}},  // tropical rainforest
    {24, 60, {0.0f, 0.0f, 0.0f}},       // hot desert (colour varies, see below)
};
const Vec3 DESERT_TAN{0.74f, 0.57f, 0.37f}, DESERT_RED{0.70f, 0.41f, 0.24f};
const Vec3 ICE{0.82f, 0.85f, 0.90f}, SEA_ICE{0.72f, 0.77f, 0.84f};
// Ocean measured against the leo_0 / earth_2 photos (tools/color_stats.py):
// deep water navy with a little teal, tropical shallows turquoise.
const Vec3 ABYSS{0.02f, 0.05f, 0.26f}, SHELF{0.03f, 0.14f, 0.36f}, SHALLOW{0.16f, 0.58f, 0.62f};
const Vec3 SILT{0.24f, 0.30f, 0.22f};
}  // namespace

#include "noise.h"

const Vec3 BEACH_SAND{0.84f, 0.80f, 0.68f};

Vec3 shallow_water(Vec3 sea, float h, float warm, float sand, float mud) {
    // Light reaching a bright bottom: tropical carbonate banks are vivid even at
    // ~40 m (clear water); temperate sand only in the first ~20 m.
    float bright = std::max(warm * smooth01(-45.0f, -3.0f, h), 0.8f * sand * smooth01(-22.0f, -2.0f, h));
    Vec3 c = mix(sea, SHALLOW, bright);
    return mix(c, SILT, mud * smooth01(-120.0f, -5.0f, h));
}

SurfaceColors surface_albedo(const Planet& P, uint32_t seed) {
    const CubeGrid& g = P.grid;
    const Vec3 off(static_cast<float>(seed % 97) * 1.37f, static_cast<float>(seed % 89) * 2.11f,
                   static_cast<float>(seed % 83) * 0.73f);
    SurfaceColors out;
    out.land.resize(g.size());
    out.ocean.resize(g.size());
    out.warm.resize(g.size());
    out.sand.resize(g.size());
    std::vector<float> land_h(g.size());
    for (int c = 0; c < g.size(); ++c) land_h[c] = std::max(P.elevation[c], 0.0f);
    #pragma omp parallel for schedule(static)
    for (int c = 0; c < g.size(); ++c) {
        Vec3 d = g.dir(c);
        float h = P.elevation[c], t = P.temperature[c];

        Vec3 sea = mix(ABYSS, SHELF, smooth01(-2500.0f, -150.0f, h));
        float ice = smooth01(-4.0f, -9.0f, t);
        out.ocean[c] = mix(sea, SEA_ICE, ice);
        out.warm[c] = smooth01(16.0f, 24.0f, t) * (1.0f - ice);
        // Sandy shores: gentle coastal slope, not frozen, drier = sandier; varies
        // along the shore so beaches alternate with rocky/marshy stretches.
        float slope = length(g.gradient(land_h, c)) / (EARTH_RADIUS_KM * 1000.0f);
        out.sand[c] = smooth01(0.012f, 0.003f, slope) * smooth01(-2.0f, 8.0f, t)
                      * (0.45f + 0.55f * smooth01(1200.0f, 300.0f, P.precip[c]))
                      * smooth01(0.38f, 0.58f, fbm3(d * 11.0f + off * 3.1f, 3));

        // Jitter the climate a little so biome borders wander and interiors vary.
        float tn = t + 3.0f * (2.0f * fbm3(d * 7.0f + off, 4) - 1.0f);
        float lp = std::log(std::max(P.precip[c], 10.0f)) + 0.35f * (2.0f * fbm3(d * 9.0f + off * 1.7f, 4) - 1.0f);
        Vec3 sum(0, 0, 0);
        float wsum = 0.0f;
        for (const Biome& b : BIOMES) {
            float dt = (tn - b.t) / 7.0f, dp = (lp - std::log(b.p)) / 0.55f;
            float w = std::exp(-(dt * dt + dp * dp)) + 1e-6f;
            Vec3 col = b.color;
            if (col.x == 0.0f) col = mix(DESERT_TAN, DESERT_RED, smooth01(0.35f, 0.65f, fbm3(d * 2.5f + off, 3)));
            sum = sum + col * w;
            wsum += w;
        }
        Vec3 col = sum / wsum * (0.94f + 0.12f * fbm3(d * 14.0f + off * 2.3f, 4));
        // Annual means hide warm summers (Siberia ~-10 C is forest, not ice):
        // only clearly frigid places hold permanent ice/snow.
        out.land[c] = mix(col, ICE, smooth01(-11.0f, -17.0f, tn));
    }
    // River plumes: runoff entering each sea cell, spread a few cells offshore.
    std::vector<float> plume(g.size(), 0.0f);
    if (!P.drainage.empty())
        for (int c = 0; c < g.size(); ++c) {
            int r = P.flow_to[c];
            if (P.elevation[c] > 0.0f && r >= 0 && P.elevation[r] <= 0.0f) plume[r] += P.drainage[c];
        }
    for (int it = 0; it < 4; ++it) {
        std::vector<float> np = plume;
        for (int c = 0; c < g.size(); ++c) {
            if (P.elevation[c] > 0.0f) continue;
            for (int nb : g.nbr(c))
                if (P.elevation[nb] <= 0.0f) np[c] = std::max(np[c], 0.55f * plume[nb]);
        }
        plume.swap(np);
    }
    std::vector<int> land, sea;
    for (int c = 0; c < g.size(); ++c) (P.elevation[c] > 0.0f ? land : sea).push_back(c);
    std::vector<float> to_land = distance_from(g, land), to_sea = distance_from(g, sea);
    out.coast_km.resize(g.size());
    for (int c = 0; c < g.size(); ++c)
        out.coast_km[c] = (P.elevation[c] > 0.0f ? to_sea[c] : to_land[c]) * EARTH_RADIUS_KM;
    out.mud.resize(g.size());
    for (int c = 0; c < g.size(); ++c) out.mud[c] = 0.7f * smooth01(3e4f, 6e5f, plume[c]);
    return out;
}
