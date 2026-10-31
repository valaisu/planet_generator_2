#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>

#include "post.h"

namespace {

// --- packing ---------------------------------------------------------------
Vec3 unpack(uint32_t px) {
    return Vec3((px & 0xFF) / 255.0f, ((px >> 8) & 0xFF) / 255.0f,
                ((px >> 16) & 0xFF) / 255.0f);
}
uint32_t pack(Vec3 c) {
    auto b = [](float v) {
        return static_cast<uint32_t>(std::clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f);
    };
    return b(c.x) | (b(c.y) << 8) | (b(c.z) << 16) | (0xFFu << 24);
}

float dist2(Vec3 a, Vec3 b) {
    Vec3 d = a - b;
    return dot(d, d);
}

// Indices of the nearest and second-nearest palette colors to `c`.
void two_nearest(const std::vector<Vec3>& pal, Vec3 c, int& i0, int& i1) {
    float d0 = 1e30f, d1 = 1e30f;
    i0 = 0; i1 = 0;
    for (int i = 0; i < static_cast<int>(pal.size()); ++i) {
        float d = dist2(pal[i], c);
        if (d < d0) { d1 = d0; i1 = i0; d0 = d; i0 = i; }
        else if (d < d1) { d1 = d; i1 = i; }
    }
}

// 4x4 Bayer matrix, normalized to (0,1).
float bayer(int x, int y) {
    static const int m[16] = {0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5};
    return (m[(y & 3) * 4 + (x & 3)] + 0.5f) / 16.0f;
}

float hash_rand(int x, int y, int iter) {
    float h = std::sin(x * 12.9898f + y * 78.233f + iter * 37.719f) * 43758.5453f;
    return h - std::floor(h);
}

// Separable box blur of radius r over a Vec3 image.
void box_blur(std::vector<Vec3>& img, int w, int h, int r) {
    if (r <= 0) return;
    std::vector<Vec3> tmp(img.size());
    auto at = [&](const std::vector<Vec3>& s, int x, int y) -> Vec3 {
        x = std::clamp(x, 0, w - 1);
        y = std::clamp(y, 0, h - 1);
        return s[static_cast<size_t>(y) * w + x];
    };
    float inv = 1.0f / (2 * r + 1);
    #pragma omp parallel for schedule(static)
    for (int y = 0; y < h; ++y)             // horizontal
        for (int x = 0; x < w; ++x) {
            Vec3 sum(0, 0, 0);
            for (int k = -r; k <= r; ++k) sum = sum + at(img, x + k, y);
            tmp[static_cast<size_t>(y) * w + x] = sum * inv;
        }
    #pragma omp parallel for schedule(static)
    for (int y = 0; y < h; ++y)             // vertical
        for (int x = 0; x < w; ++x) {
            Vec3 sum(0, 0, 0);
            for (int k = -r; k <= r; ++k) sum = sum + at(tmp, x, y + k);
            img[static_cast<size_t>(y) * w + x] = sum * inv;
        }
}

}  // namespace

void apply_post(const PostProcess& pp, int w, int h, std::vector<uint32_t>& pixels) {
    if (!pp.enabled || pp.palette.empty()) return;
    if (w <= 0 || h <= 0) return;

    std::vector<Vec3> img(pixels.size());
    for (size_t i = 0; i < pixels.size(); ++i) img[i] = unpack(pixels[i]);

    int iters = std::max(1, pp.iterations);
    int radius = std::max(0, static_cast<int>(pp.blur_radius + 0.5f));
    for (int it = 0; it < iters; ++it) {
        box_blur(img, w, h, radius);
        #pragma omp parallel for schedule(static)
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x) {
                size_t idx = static_cast<size_t>(y) * w + x;
                int i0, i1;
                two_nearest(pp.palette, img[idx], i0, i1);
                Vec3 c0 = pp.palette[i0];
                if (pp.dither == DitherMode::None || i0 == i1) {
                    img[idx] = c0;
                    continue;
                }
                // Blend factor: how far the pixel sits from c0 toward c1, by
                // projecting onto the c0->c1 segment. Threshold it against the
                // dither pattern to pick one of the two nearest colors.
                Vec3 c1 = pp.palette[i1];
                Vec3 seg = c1 - c0;
                float len2 = dot(seg, seg);
                float tblend = (len2 > 1e-6f) ? dot(img[idx] - c0, seg) / len2 : 0.0f;
                tblend = std::clamp(tblend, 0.0f, 1.0f);
                float th = (pp.dither == DitherMode::Ordered) ? bayer(x, y)
                                                              : hash_rand(x, y, it);
                img[idx] = (tblend > th) ? c1 : c0;
            }
    }

    for (size_t i = 0; i < pixels.size(); ++i) pixels[i] = pack(img[i]);
}

std::vector<Vec3> kmeans_palette(const std::vector<Vec3>& samples, int k, uint32_t seed) {
    std::vector<Vec3> cent;
    if (samples.empty() || k <= 0) return cent;
    uint32_t s = seed * 747796405u + 2891336453u;
    auto rnd = [&]() {  // LCG -> [0,1)
        s = s * 1664525u + 1013904223u;
        return (s >> 8) * (1.0f / 16777216.0f);
    };
    const size_t n = samples.size();

    // k-means++ init: each next center drawn proportional to squared distance.
    cent.push_back(samples[static_cast<size_t>(rnd() * n) % n]);
    std::vector<float> d2(n);
    while (static_cast<int>(cent.size()) < k) {
        double total = 0.0;
        for (size_t i = 0; i < n; ++i) {
            float best = 1e30f;
            for (const Vec3& c : cent) best = std::min(best, dist2(samples[i], c));
            d2[i] = best;
            total += best;
        }
        if (total <= 0.0) break;  // fewer distinct colors than k
        double pick = rnd() * total;
        size_t i = 0;
        for (; i + 1 < n; ++i) {
            pick -= d2[i];
            if (pick <= 0.0) break;
        }
        cent.push_back(samples[i]);
    }

    // Lloyd iterations.
    std::vector<int> label(n, 0);
    for (int it = 0; it < 30; ++it) {
        bool changed = false;
        for (size_t i = 0; i < n; ++i) {
            int bi = 0;
            float bd = 1e30f;
            for (int j = 0; j < static_cast<int>(cent.size()); ++j) {
                float d = dist2(samples[i], cent[j]);
                if (d < bd) { bd = d; bi = j; }
            }
            if (label[i] != bi) { label[i] = bi; changed = true; }
        }
        std::vector<Vec3> sum(cent.size(), Vec3(0, 0, 0));
        std::vector<int> cnt(cent.size(), 0);
        for (size_t i = 0; i < n; ++i) { sum[label[i]] = sum[label[i]] + samples[i]; ++cnt[label[i]]; }
        for (size_t j = 0; j < cent.size(); ++j)
            if (cnt[j] > 0) cent[j] = sum[j] / static_cast<float>(cnt[j]);
        if (!changed && it > 0) break;
    }

    auto luma = [](Vec3 c) { return 0.299f * c.x + 0.587f * c.y + 0.114f * c.z; };
    std::sort(cent.begin(), cent.end(), [&](Vec3 a, Vec3 b) { return luma(a) < luma(b); });
    return cent;
}

bool save_palette(const std::string& path, const std::vector<Vec3>& pal) {
    std::ofstream f(path);
    if (!f) return false;
    for (const Vec3& c : pal) {
        uint32_t p = pack(c);
        char buf[16];
        std::snprintf(buf, sizeof(buf), "#%02x%02x%02x", p & 0xFF, (p >> 8) & 0xFF, (p >> 16) & 0xFF);
        f << buf << "\n";
    }
    return true;
}

bool load_palette(const std::string& path, std::vector<Vec3>& pal) {
    std::ifstream f(path);
    if (!f) return false;
    pal.clear();
    std::string line;
    while (std::getline(f, line)) {
        unsigned r, g, b;
        if (std::sscanf(line.c_str(), " #%02x%02x%02x", &r, &g, &b) == 3)
            pal.push_back(Vec3(r / 255.0f, g / 255.0f, b / 255.0f));
    }
    return !pal.empty();
}
