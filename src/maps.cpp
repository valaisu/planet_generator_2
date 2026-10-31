#include <algorithm>
#include <cmath>
#include <vector>

#include "image_io.h"
#include "maps.h"
#include "render.h"
#include "surface.h"

namespace {
constexpr float PI = 3.14159265358979323846f;

Vec3 lerp(Vec3 a, Vec3 b, float t) { return a + (b - a) * std::clamp(t, 0.0f, 1.0f); }

// Piecewise-linear ramp through (value, colour) stops.
Vec3 ramp(const std::vector<std::pair<float, Vec3>>& stops, float v) {
    if (v <= stops.front().first) return stops.front().second;
    for (size_t i = 1; i < stops.size(); ++i)
        if (v <= stops[i].first)
            return lerp(stops[i - 1].second, stops[i].second,
                        (v - stops[i - 1].first) / (stops[i].first - stops[i - 1].first));
    return stops.back().second;
}

Vec3 hash_color(int k) {
    uint32_t h = static_cast<uint32_t>(k) * 2654435761u;
    h ^= h >> 15;
    h *= 2246822519u;
    h ^= h >> 13;
    return Vec3(0.25f + 0.7f * ((h & 255) / 255.0f), 0.25f + 0.7f * (((h >> 8) & 255) / 255.0f),
                0.25f + 0.7f * (((h >> 16) & 255) / 255.0f));
}
}  // namespace

Vec3 elevation_color(float m) {
    if (m <= 0.0f)
        return ramp({{-6000, {0.02f, 0.04f, 0.16f}}, {-3000, {0.05f, 0.12f, 0.35f}},
                     {-200, {0.15f, 0.35f, 0.62f}}, {0, {0.35f, 0.6f, 0.8f}}}, m);
    return ramp({{0, {0.25f, 0.45f, 0.22f}}, {500, {0.5f, 0.6f, 0.3f}}, {1200, {0.7f, 0.6f, 0.38f}},
                 {2500, {0.55f, 0.4f, 0.3f}}, {4000, {0.95f, 0.95f, 0.95f}}}, m);
}

bool write_map(const std::string& path, int w, const std::function<Vec3(Vec3)>& color) {
    int h = w / 2;
    std::vector<uint32_t> px(static_cast<size_t>(w) * h);
    #pragma omp parallel for schedule(static)
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            float lon = ((x + 0.5f) / w * 2.0f - 1.0f) * PI;
            float lat = (0.5f - (y + 0.5f) / h) * PI;
            Vec3 d(std::cos(lat) * std::sin(lon), std::sin(lat), std::cos(lat) * std::cos(lon));
            px[static_cast<size_t>(y) * w + x] = pack_rgba(color(d));
        }
    return write_png(path, w, h, px);
}

void write_planet_maps(const Planet& P, const std::string& dir) {
    const CubeGrid& g = P.grid;
    const int W = 720;
    write_map(dir + "/elevation.png", W, [&](Vec3 d) { return elevation_color(g.sample(P.elevation, d)); });
    {
        std::vector<Vec3> nrm = relief_normals(P, 40.0f);
        write_map(dir + "/relief.png", W * 2, [&](Vec3 d) {
            Vec3 e, n;
            CubeGrid::tangent_basis(d, e, n);
            Vec3 light = normalize(d * 0.6f - e + n);  // from the north-west
            float shade = std::max(0.0f, dot(normalize(g.sample(nrm, d)), light)) / std::max(0.2f, dot(d, light));
            return elevation_color(g.sample(P.elevation, d)) * std::min(1.6f, 0.15f + 0.85f * shade);
        });
    }
    // Rivers (drainage >= 1.5e4, as rendered; log scale) + cities (orange, brighter = bigger) over
    // habitability (dark green = empty, light = favourable).
    if (!P.drainage.empty()) {
        std::vector<float> city_cell(g.size(), 0.0f);
        for (const City& ct : P.cities) city_cell[g.cell_of(ct.dir)] += ct.size;
        write_map(dir + "/rivers_cities.png", W * 2, [&](Vec3 d) {
            int c = g.cell_of(d);
            if (P.elevation[c] <= 0.0f) return Vec3(0.05f, 0.07f, 0.15f);
            if (city_cell[c] > 0.0f) return Vec3(1.0f, 0.6f, 0.2f) * (0.6f + 0.4f * std::min(1.0f, city_cell[c] / 30.0f));
            float r = std::clamp((std::log10(std::max(P.drainage[c], 1.0f)) - 4.17f) / 2.0f, 0.0f, 1.0f);
            if (r > 0.0f) return lerp(Vec3(0.2f, 0.5f, 0.9f), Vec3(0.6f, 0.9f, 1.0f), r);
            float hab = std::clamp(P.habitability[c] / 3.0f, 0.0f, 1.0f);
            return lerp(Vec3(0.12f, 0.16f, 0.10f), Vec3(0.55f, 0.65f, 0.4f), hab);
        });
    }
    if (!P.plate.empty() && P.plate[0] >= 0) {
        write_map(dir + "/plates.png", W, [&](Vec3 d) {
            int c = g.cell_of(d);
            Vec3 col = hash_color(P.plate[c]) * (P.continental[c] ? 1.0f : 0.55f);
            float cv = P.convergence[c];
            if (cv > 0.1f) return Vec3(1.0f, 0.15f, 0.1f);   // converging
            if (cv < -0.1f) return Vec3(0.1f, 0.9f, 1.0f);   // diverging
            if (cv != 0.0f) return Vec3(1, 1, 1);            // transform-ish
            return col;
        });
    }
    if (!P.temperature.empty())
        write_map(dir + "/temperature.png", W, [&](Vec3 d) {
            float t = g.sample(P.temperature, d);
            return ramp({{-40, {0.9f, 0.9f, 1.0f}}, {-15, {0.3f, 0.3f, 0.9f}}, {0, {0.2f, 0.7f, 0.9f}},
                         {10, {0.3f, 0.8f, 0.3f}}, {20, {0.95f, 0.85f, 0.2f}}, {30, {0.9f, 0.2f, 0.1f}}}, t);
        });
    if (!P.pressure.empty())
        write_map(dir + "/pressure_wind.png", W, [&](Vec3 d) {
            float p = g.sample(P.pressure, d);
            Vec3 col = ramp({{995, {0.2f, 0.2f, 0.8f}}, {1013, {0.9f, 0.9f, 0.9f}}, {1030, {0.85f, 0.2f, 0.2f}}}, p);
            if (g.sample(P.elevation, d) > 0) col = col * 0.75f;
            // Wind streak overlay: brighten along a hashed grid of short dashes.
            Vec3 e, n;
            CubeGrid::tangent_basis(d, e, n);
            Vec3 wv = g.sample(P.wind, d);
            float lon = std::atan2(d.x, d.z), lat = std::asin(d.y);
            float cx = std::fmod(lon * 18.0f / PI + 100.0f, 1.0f) - 0.5f;
            float cy = std::fmod(lat * 18.0f / PI + 100.0f, 1.0f) - 0.5f;
            float we = dot(wv, e), wn = dot(wv, n), wl = std::sqrt(we * we + wn * wn) + 1e-6f;
            float along = (cx * we + cy * wn) / wl, across = (-cx * wn + cy * we) / wl;
            if (std::fabs(across) < 0.06f && along > -0.4f && along < 0.4f * std::min(1.0f, wl * 2.0f))
                col = lerp(Vec3(0, 0, 0), Vec3(1, 1, 0), 0.5f + along);
            return col;
        });
    if (!P.precip.empty())
        write_map(dir + "/precip.png", W, [&](Vec3 d) {
            float p = g.sample(P.precip, d);
            Vec3 col = ramp({{0, {0.75f, 0.55f, 0.3f}}, {250, {0.9f, 0.8f, 0.45f}}, {600, {0.55f, 0.8f, 0.35f}},
                             {1200, {0.15f, 0.6f, 0.25f}}, {2500, {0.05f, 0.35f, 0.4f}}, {4000, {0.1f, 0.1f, 0.5f}}}, p);
            return g.sample(P.elevation, d) > 0 ? col : col * 0.45f;
        });
    if (!P.precip.empty()) {
        SurfaceColors alb = surface_albedo(P, 1);
        write_map(dir + "/albedo.png", W, [&](Vec3 d) { return alb.at(P, g.cell_of(d)) * 1.3f; });
    }
    if (!P.humidity.empty())
        write_map(dir + "/humidity.png", W, [&](Vec3 d) {
            float h = g.sample(P.humidity, d);
            Vec3 col = ramp({{0, {0.6f, 0.4f, 0.2f}}, {0.5f, {0.9f, 0.9f, 0.6f}}, {1, {0.1f, 0.3f, 0.9f}}}, h);
            return g.sample(P.elevation, d) > 0 ? col : col * 0.5f;
        });
    if (!P.weather_vorticity.empty())
        write_map(dir + "/vorticity.png", W, [&](Vec3 d) {
            float z = g.sample(P.weather_vorticity, d) * (d.y >= 0 ? 1.0f : -1.0f);  // >0 cyclonic
            return ramp({{-8, {0.1f, 0.2f, 0.8f}}, {0, {0.9f, 0.9f, 0.9f}}, {8, {0.85f, 0.15f, 0.1f}}}, z);
        });
    if (!P.cloud.empty())  // raw opacity, greyscale (input for tools/cloud_stats.py)
        write_map(dir + "/cloud_alpha.png", W, [&](Vec3 d) {
            float a = g.sample(P.cloud, d);
            return Vec3(a, a, a);
        });
    if (!P.cloud.empty())
        write_map(dir + "/cloud.png", W, [&](Vec3 d) {
            float c = g.sample(P.cloud, d);
            return lerp(elevation_color(g.sample(P.elevation, d)) * 0.6f, Vec3(1, 1, 1), c);
        });
}
