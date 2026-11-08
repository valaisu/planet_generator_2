#include "color_profile.h"

#include <algorithm>
#include <cmath>
#include <fstream>

#include "noise.h"

namespace {
// sRGB <-> CIELAB (D65), as tools/evaluate.py.
void to_lab(uint32_t p, float& L, float& A, float& B) {
    float c[3];
    for (int k = 0; k < 3; ++k) {
        float v = ((p >> (8 * k)) & 255) / 255.0f;
        c[k] = v <= 0.04045f ? v / 12.92f : std::pow((v + 0.055f) / 1.055f, 2.4f);
    }
    float x = (0.4124f * c[0] + 0.3576f * c[1] + 0.1805f * c[2]) / 0.9505f;
    float y = 0.2126f * c[0] + 0.7152f * c[1] + 0.0722f * c[2];
    float z = (0.0193f * c[0] + 0.1192f * c[1] + 0.9505f * c[2]) / 1.089f;
    auto f = [](float t) { return t > 0.008856f ? std::cbrt(t) : 7.787f * t + 16.0f / 116.0f; };
    L = 116.0f * f(y) - 16.0f;
    A = 500.0f * (f(x) - f(y));
    B = 200.0f * (f(y) - f(z));
}

uint32_t from_lab(float L, float A, float B, uint32_t alpha) {
    float fy = (L + 16.0f) / 116.0f, fx = fy + A / 500.0f, fz = fy - B / 200.0f;
    auto inv = [](float t) { return t > 0.2069f ? t * t * t : (t - 16.0f / 116.0f) / 7.787f; };
    float x = inv(fx) * 0.9505f, y = inv(fy), z = inv(fz) * 1.089f;
    float c[3] = {3.2406f * x - 1.5372f * y - 0.4986f * z, -0.9689f * x + 1.8758f * y + 0.0415f * z,
                  0.0557f * x - 0.2040f * y + 1.0570f * z};
    uint32_t out = alpha;
    for (int k = 0; k < 3; ++k) {
        float v = std::clamp(c[k], 0.0f, 1.0f);
        v = v <= 0.0031308f ? 12.92f * v : 1.055f * std::pow(v, 1.0f / 2.4f) - 0.055f;
        out |= static_cast<uint32_t>(std::lround(v * 255.0f)) << (8 * k);
    }
    return out;
}

float smooth01(float a, float b, float x) {
    float t = std::clamp((x - a) / (b - a), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

// Value at quantile q (0..1) of `n` uniform samples starting at v.
float at_quantile(const float* v, int n, float q) {
    float x = std::clamp(q * n - 0.5f, 0.0f, n - 1.0f);
    int i = std::min(static_cast<int>(x), n - 2);
    return v[i] + (v[i + 1] - v[i]) * (x - i);
}

// Quantile of each value among the sorted reference values.
float quantile_in(const std::vector<float>& sorted, float v) {
    return static_cast<float>(std::lower_bound(sorted.begin(), sorted.end(), v) - sorted.begin()) / sorted.size();
}
}  // namespace

bool ColorProfile::load(const std::string& path) {
    std::ifstream in(path);
    std::string tag;
    int nl = 0;
    if (!(in >> tag >> bands >> samples >> nl) || tag != "colorprofile" || bands < 1 || samples < 2 || nl < 2) return false;
    L.resize(nl);
    for (float& v : L) in >> v;
    dir.resize(2 * bands);
    a.resize(static_cast<size_t>(bands) * samples);
    b.resize(a.size());
    for (int k = 0; k < bands; ++k) {
        in >> dir[2 * k] >> dir[2 * k + 1];
        for (int i = 0; i < samples; ++i) in >> a[static_cast<size_t>(k) * samples + i];
        for (int i = 0; i < samples; ++i) in >> b[static_cast<size_t>(k) * samples + i];
    }
    return static_cast<bool>(in);
}

void ColorProfile::apply(std::vector<uint32_t>& px, const std::vector<bool>& planet, const std::vector<bool>& inner) const {
    const size_t n = px.size();
    std::vector<float> l(n), A(n), B(n), ice(n, 0.0f);
    std::vector<float> ref;
    for (size_t i = 0; i < n; ++i) {
        if (!planet[i]) continue;
        to_lab(px[i], l[i], A[i], B[i]);
        l[i] += 1e-3f * (hash11(static_cast<float>(i) * 0.618f) - 0.5f);  // breaks ties in flat regions
        // Ice and frost (near-white: chroma / L below ~0.3; rock and dust ~0.5+)
        // keep their modelled colour and stay out of the statistics: the
        // profile comes from the photo's interior, which has hardly any ice.
        ice[i] = smooth01(0.4f, 0.2f, std::hypot(A[i], B[i]) / std::max(l[i], 1.0f));
        if (inner[i] && ice[i] < 0.5f) ref.push_back(l[i]);
    }
    if (ref.size() < 16) return;
    std::sort(ref.begin(), ref.end());
    std::vector<float> q(n, 0.0f);
    std::vector<std::vector<float>> keys(bands);
    std::vector<int> band(n, 0);
    for (size_t i = 0; i < n; ++i) {
        if (!planet[i]) continue;
        q[i] = quantile_in(ref, l[i]);
        band[i] = std::min(static_cast<int>(q[i] * bands), bands - 1);
        if (inner[i] && ice[i] < 0.5f) keys[band[i]].push_back(A[i] * dir[2 * band[i]] + B[i] * dir[2 * band[i] + 1]);
    }
    for (auto& k : keys) std::sort(k.begin(), k.end());
    // Brightness: halfway between the photo's by rank and a mean/spread match
    // that never stretches: rank matching alone forces the photo's full range on
    // every face, so a face without much albedo contrast got its relief shading
    // stretched to it.
    double pm = 0.0, pv = 0.0, rm = 0.0, rv = 0.0;
    for (float v : L) pm += v;
    pm /= L.size();
    for (float v : L) pv += (v - pm) * (v - pm);
    for (float v : ref) rm += v;
    rm /= ref.size();
    for (float v : ref) rv += (v - rm) * (v - rm);
    const float stretch = static_cast<float>(std::min(1.0, std::sqrt((pv / L.size()) / std::max(rv / ref.size(), 1e-6))));
    for (size_t i = 0; i < n; ++i) {
        if (!planet[i]) continue;
        const int k = band[i];
        float key = A[i] * dir[2 * k] + B[i] * dir[2 * k + 1];
        float qk = keys[k].empty() ? 0.5f : quantile_in(keys[k], key);
        const float* as = &a[static_cast<size_t>(k) * samples];
        const float* bs = &b[static_cast<size_t>(k) * samples];
        float l_rank = at_quantile(L.data(), static_cast<int>(L.size()), q[i]);
        float l_affine = static_cast<float>(pm) + (l[i] - static_cast<float>(rm)) * stretch;
        const float w = ice[i];
        px[i] = from_lab(0.5f * (l_rank + l_affine) * (1.0f - w) + l[i] * w, at_quantile(as, samples, qk) * (1.0f - w) + A[i] * w,
                         at_quantile(bs, samples, qk) * (1.0f - w) + B[i] * w, px[i] & 0xff000000u);
    }
}
