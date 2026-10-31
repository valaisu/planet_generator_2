// Real Earth relief (tools/convert_relief.py output) resampled onto the grid, so
// the climate/colour stages can be validated on the one planet we know.
#include <cmath>
#include <cstdint>
#include <cstdio>

#include "planet.h"

bool load_earth_terrain(Planet& P, const std::string& path) {
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    int32_t wh[2];
    bool ok = std::fread(wh, sizeof(int32_t), 2, f) == 2;
    const int w = wh[0], h = wh[1];  // gridline registered: lon -180..180, lat 90..-90
    std::vector<float> z(ok ? static_cast<size_t>(w) * h : 0);
    ok = ok && std::fread(z.data(), sizeof(float), z.size(), f) == z.size();
    std::fclose(f);
    if (!ok) return false;

    const CubeGrid& g = P.grid;
    constexpr float PI = 3.14159265358979323846f;
    P.elevation.resize(g.size());
    P.region.assign(g.size(), -1);
    P.plate.assign(g.size(), -1);
    P.continental.assign(g.size(), 0);
    P.plate_velocity.assign(g.size(), Vec3(0, 0, 0));
    P.convergence.assign(g.size(), 0.0f);
    for (int c = 0; c < g.size(); ++c) {
        Vec3 d = g.dir(c);
        float lat = std::asin(std::clamp(d.y, -1.0f, 1.0f));
        float lon = std::atan2(d.x, d.z);
        float fx = (lon / PI + 1.0f) * 0.5f * (w - 1);
        float fy = (0.5f - lat / PI) * (h - 1);
        int x0 = std::min(static_cast<int>(fx), w - 2), y0 = std::min(static_cast<int>(fy), h - 2);
        float tx = fx - x0, ty = fy - y0;
        auto at = [&](int x, int y) { return z[static_cast<size_t>(y) * w + x]; };
        float top = at(x0, y0) * (1 - tx) + at(x0 + 1, y0) * tx;
        float bot = at(x0, y0 + 1) * (1 - tx) + at(x0 + 1, y0 + 1) * tx;
        P.elevation[c] = top * (1 - ty) + bot * ty;
        P.continental[c] = P.elevation[c] > -200.0f;
    }
    return true;
}

// Diagnostic: replace the simulated clouds with a real equirectangular cloud
// photo (e.g. NASA Blue Marble cloud_combined), brightness -> opacity.
#include "stb/stb_image.h"

bool load_cloud_image(Planet& P, const std::string& path) {
    int w, h, ch;
    unsigned char* img = stbi_load(path.c_str(), &w, &h, &ch, 1);
    if (!img) return false;
    constexpr float PI = 3.14159265358979323846f;
    const CubeGrid& g = P.grid;
    P.cloud.resize(g.size());
    for (int c = 0; c < g.size(); ++c) {
        Vec3 d = g.dir(c);
        float lat = std::asin(std::clamp(d.y, -1.0f, 1.0f)), lon = std::atan2(d.x, d.z);
        int x = std::clamp(static_cast<int>((lon / PI + 1.0f) * 0.5f * w), 0, w - 1);
        int y = std::clamp(static_cast<int>((0.5f - lat / PI) * h), 0, h - 1);
        float v = img[static_cast<size_t>(y) * w + x] / 255.0f;
        P.cloud[c] = std::clamp((v - 0.08f) / 0.8f, 0.0f, 1.0f);
    }
    stbi_image_free(img);
    return true;
}
