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

namespace {
// Lifeless worlds: the material model (WorldSpec colours) mixed per cell. Regions
// are soft and domain-warped (fractal edges, no thresholded blobs) and mottled
// inside; the terrain modulates albedo by process (slope_albedo, crest_albedo);
// small-scale variation down to the grid (1-3 disk pixels) carries most of the
// look. Layers: base; dark regions (~dark_amount, out of young plains); stains
// (ice-shell cracks + chaos, or a low-latitude tholin belt outside the young
// basin); fresh-crater rays; ice (cap edge / volatile ice in basin and toward
// the poles). `slope` (0..1+) and `crest` (~-2..2, local relief in sd) per cell.
// (fbm3 with 4-5 octaves: median 0.5, sd ~0.12.)
Vec3 material_color(const Planet& P, int c, Vec3 d, Vec3 off, float slope, float crest, float lows, float pole_face, float dust) {
    const WorldSpec& W = P.world;
    auto share = [](float amount) { return 0.5f + 0.12f * 1.6f * (1.0f - 2.0f * amount); };  // fbm threshold
    auto warp = [&](Vec3 p, float k) {
        return normalize(p + Vec3(fbm3(p * 4.0f + off, 3) - 0.5f, fbm3(p * 4.0f + off * 1.3f + Vec3(5, 1, 0), 3) - 0.5f,
                                  fbm3(p * 4.0f + off * 0.7f + Vec3(0, 9, 2), 3) - 0.5f) * k);
    };
    Vec3 q = warp(d, 0.35f);
    float fine = fbm3(d * 24.0f + off * 3.1f, 3);  // ~1-3 px; finer noise only dithers into grain
    Vec3 col = W.rock;
    float young = P.resurfaced.empty() ? 0.0f : P.resurfaced[c];
    // Dark regions: soft edges, broken up inside by the fine noise.
    float t = share(W.dark_amount);
    float dk = smooth01(t - 0.08f * W.edge, t + 0.08f * W.edge, fbm3(q * 3.0f + off, 5) + 0.2f * (fine - 0.5f));
    float dark_w = dk * (1.0f - 0.7f * young);
    if (W.dust > 0.0f) {  // dusty air: the wind decides (see dust_cover) ...
        dark_w = 1.0f - dust;
        // ... and dark sand collects in hollows at every scale (lows between
        // knobs, big crater floors and basins: `lows` ~100 km; dunes on the
        // downwind side of crater floors), less where dust blankets everything.
        float hollow = 0.3f * smooth01(-0.3f, -1.5f, crest) + 0.7f * smooth01(-0.3f, -1.5f, lows);
        float floor_sand = P.crater_sand.empty() ? 0.0f : P.crater_sand[c];
        dark_w += W.sand_lows * (hollow + floor_sand) * (0.15f + 0.85f * (1.0f - dust));
    }
    if (W.slope_albedo < 0.0f) dark_w += -W.slope_albedo * std::min(1.0f, slope) * (0.5f + 0.5f * smooth01(0.0f, 1.5f, crest));
    col = mix(col, W.dark * W.dark_level, W.dark_mix * std::clamp(dark_w, 0.0f, 1.0f));
    if (W.alt_amount > 0.0f) {  // third material: new hue, same brightness
        float ta = share(W.alt_amount);
        float aw = smooth01(ta - 0.10f * W.edge, ta + 0.10f * W.edge, fbm3(q * 1.7f + off * 3.7f, 4) + 0.2f * (fine - 0.5f));
        auto luma = [](Vec3 v) { return 0.299f * v.x + 0.587f * v.y + 0.114f * v.z; };
        col = mix(col, W.alt * (luma(col) / std::max(1e-3f, luma(W.alt))), aw);
    }
    if (W.stain_amount > 0.0f) {
        float st;
        if (W.cracks) {
            // mottled chaos: broad soft regions speckled at small scale
            float ts = share(W.stain_amount);
            // two scales: broad regions + small patches inside and around them
            float field = 0.65f * fbm3(q * 2.5f + off * 1.9f, 5) + 0.35f * fbm3(d * 9.0f + off * 4.4f, 3);
            float chaos = smooth01(ts - 0.12f * W.edge, ts + 0.12f * W.edge, field);
            chaos *= 0.4f + 1.2f * fine;
            st = std::max(P.crack[c], 0.75f * std::clamp(chaos, 0.0f, 1.0f));
        } else {
            // a low-latitude belt, broken in places (Cthulhu), mottled
            float lat = std::fabs(std::asin(q.y)) * 57.3f;
            float ts = share(W.stain_amount);
            float belt = smooth01(40.0f, 15.0f, lat + 25.0f * (fbm3(q * 2.0f + off, 4) - 0.5f));
            st = belt * (1.0f - young) * smooth01(ts - 0.07f * W.edge, ts + 0.07f * W.edge, fbm3(q * 1.2f + off * 2.7f, 4)) * (0.7f + 0.6f * fine);
        }
        col = mix(col, W.stain, std::clamp(st, 0.0f, 1.0f));
    }
    if (W.slope_albedo > 0.0f) col = mix(col, W.bright, W.slope_albedo * std::min(1.0f, slope));
    // Rays show where fresh ejecta are brighter than the weathered surface
    // (dark rock); on bright ice they barely register.
    auto lum = [](Vec3 v) { return 0.299f * v.x + 0.587f * v.y + 0.114f * v.z; };
    float ray_contrast = std::clamp((lum(W.bright) - lum(col)) / 0.25f, 0.0f, 1.0f);
    if (W.glacier_basin) ray_contrast = 0.0f;  // volatile frost transport blankets fresh ejecta
    if (!P.ray.empty()) col = mix(col, W.bright, 0.85f * ray_contrast * P.ray[c]);
    if (!P.streak.empty()) col = mix(col, W.bright, std::min(1.0f, 0.45f * W.streak_strength * P.streak[c]));
    float ice = smooth01(W.ice_below + 3.0f, W.ice_below - 3.0f, P.temperature[c] - 4.0f * (fine - 0.5f) / 0.12f);
    if (W.glacier_basin) {
        float lat = std::fabs(std::asin(q.y)) * 57.3f;
        ice = std::max(young, smooth01(45.0f, 62.0f, lat + 20.0f * (fbm3(q * 3.0f + off * 0.3f, 4) - 0.5f)) * (0.5f + fine));
    }
    if (W.crest_albedo > 0.0f) ice = std::max(ice, W.crest_albedo * smooth01(0.5f, 2.0f, crest));
    if (W.frost_lat < 90.0f && d.y < 0.0f) {  // winter (southern) seasonal frost cap, ragged edge
        // Frost lasts longest in the cold spots: pole-facing slopes and hollows,
        // so the cap edge breaks into frosted crater walls and floors.
        float cold = 6.0f * pole_face + 4.0f * smooth01(-0.3f, -1.5f, lows) + 3.0f * smooth01(-0.3f, -1.5f, crest);
        float lat = -std::asin(std::clamp(q.y, -1.0f, 0.0f)) * 57.2958f + 10.0f * (fine - 0.5f) / 0.12f * 0.3f + cold;
        float fr = smooth01(W.frost_lat - 2.0f, W.frost_lat + 4.0f, lat);
        col = mix(col, W.frost, 0.9f * fr);
    }
    col = mix(col, W.ice, std::clamp(ice, 0.0f, 1.0f));
    // Small-scale mottling (the 1-3 px texture) on top of the gentle base noise.
    float l = lum(col);
    col = Vec3(l, l, l) + (col - Vec3(l, l, l)) * W.saturation;
    return col * (1.0f + 0.12f * W.grain * (fbm3(d * 14.0f + off * 2.3f, 4) - 0.5f) + 0.8f * W.mottle * (fine - 0.5f));
}

// Dust cover 0..1 per cell (WorldSpec::dust), from the ground dust the
// simulated weather laid down (clouds.cpp; smoothed here over ~100 km: it lives
// on a coarse grid), with local terrain shaping the edges (dust stays on local
// highs, is swept out of the lows: `lows` in sd), by area rank: `dark_amount` of
// the ground below the midpoint, `dust_soft` = half-width of the transition in
// rank (small: two materials, large: continuous gradations from bare to mantled).
std::vector<float> dust_cover(const Planet& P, const std::vector<float>& lows) {
    const CubeGrid& g = P.grid;
    const WorldSpec& W = P.world;
    const int N = g.size();
    if (P.wind_dust.empty()) return std::vector<float>(N, 1.0f);
    std::vector<float> wd = P.wind_dust, nx(N);
    const int iters = static_cast<int>(std::lround(8.0f * (g.n() / 256.0f) * (g.n() / 256.0f)));
    for (int it = 0; it < iters; ++it) {
        #pragma omp parallel for schedule(static)
        for (int c = 0; c < N; ++c) {
            float sum = wd[c];
            for (int nb : g.nbr(c)) sum += wd[nb];
            nx[c] = sum / 9.0f;
        }
        wd.swap(nx);
    }
    std::vector<std::pair<float, int>> order(N);
    for (int c = 0; c < N; ++c) order[c] = {std::log(std::max(wd[c], 1e-4f)) + 0.3f * std::clamp(lows[c], -3.0f, 3.0f), c};
    std::sort(order.begin(), order.end());
    std::vector<float> dust(N);
    double acc = 0.0, total = 0.0;
    for (int c = 0; c < N; ++c) total += g.area(c);
    for (const auto& [v, c] : order) {
        float rank = static_cast<float>((acc + 0.5 * g.area(c)) / total);
        acc += g.area(c);
        dust[c] = smooth01(W.dark_amount - W.dust_soft, W.dark_amount + W.dust_soft, rank);
    }
    return dust;
}
}  // namespace

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
    // Lifeless worlds: slope (normalised by its 90th percentile) and local relief
    // (elevation minus a ~3-cell blur, in standard deviations) for material_color.
    // `lows`: the same at ~100 km (dusty worlds; frost, dark sand). Blur widths
    // are fixed in km: iterations x (grid / 256)^2, so every grid gives one planet.
    std::vector<float> slope_n, crest_n, lows_n, band_n, pole_n;
    if (!P.world.biomes) {
        const int N = g.size();
        slope_n.resize(N);
        const float km_scale = (g.n() / 256.0f) * (g.n() / 256.0f);
        auto relief_at = [&](int iters) {
            std::vector<float> b = P.elevation, nx(N);
            for (int it = 0; it < static_cast<int>(std::lround(iters * km_scale)); ++it) {
                #pragma omp parallel for schedule(static)
                for (int c = 0; c < N; ++c) {
                    float sum = b[c];
                    for (int nb : g.nbr(c)) sum += b[nb];
                    nx[c] = sum / 9.0f;
                }
                b.swap(nx);
            }
            double var = 0.0;
            for (int c = 0; c < N; ++c) { b[c] = P.elevation[c] - b[c]; var += b[c] * b[c]; }
            float sd = static_cast<float>(std::sqrt(var / N)) + 1e-3f;
            for (float& v : b) v /= sd;
            return b;
        };
        crest_n = relief_at(3);
        if (P.world.dust > 0.0f) {
            lows_n = relief_at(30);
            // 40-100 km band (no narrow ridges or rims) for the dust edges
            std::vector<float> fine = relief_at(8);
            band_n.resize(N);
            for (int c = 0; c < N; ++c) band_n[c] = lows_n[c] - 0.5f * fine[c];
        }
        #pragma omp parallel for schedule(static)
        for (int c = 0; c < N; ++c) slope_n[c] = length(g.gradient(P.elevation, c));
        std::vector<float> tmp = slope_n;
        std::nth_element(tmp.begin(), tmp.begin() + N * 9 / 10, tmp.end());
        float s90 = tmp[N * 9 / 10] + 1e-6f;
        for (float& v : slope_n) v /= s90;
        if (P.world.frost_lat < 90.0f) {  // slope toward the south pole (the frosted one), -1..1
            pole_n.resize(N);
            #pragma omp parallel for schedule(static)
            for (int c = 0; c < N; ++c) {
                Vec3 d = g.dir(c), north = Vec3(0, 1, 0) - d * d.y;
                pole_n[c] = std::clamp(dot(g.gradient(P.elevation, c), north) / (s90 * (length(north) + 1e-6f)), -1.0f, 1.0f);
            }
        }
    }
    std::vector<float> dust;
    if (P.world.dust > 0.0f && !slope_n.empty()) dust = dust_cover(P, band_n);
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
        if (!P.world.biomes)
            out.land[c] = material_color(P, c, d, off, slope_n[c], crest_n[c], lows_n.empty() ? 0.0f : lows_n[c],
                                          pole_n.empty() ? 0.0f : pole_n[c], dust.empty() ? 0.0f : dust[c]);
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
