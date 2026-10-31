#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "vec3.h"

// Shared no-trig procedural noise. Extracted from the original sphere texture path
// so both the surface shader (sphere.cpp) and the per-body bake can use one
// implementation. Functions are `inline` so the header can be included by multiple
// translation units without ODR violations.
//
// NOTE (gotcha carried over): the integer bit-mix below is NOT std::sin-based — the
// texture path calls it dozens of times per pixel and trig dominated the cost.
// Changing the mix reshuffles every lattice value (a seed renders differently), so
// don't swap it casually.

// Integer bit-mix (Murmur-style finalizer) -> well-distributed uint32.
inline uint32_t mix32(uint32_t h) {
    h ^= h >> 16; h *= 0x7feb352du; h ^= h >> 15; h *= 0x846ca68bu; h ^= h >> 16;
    return h;
}

// Quantize a coordinate to a lattice key (fractional part preserved x4096).
inline uint32_t noise_key(float v) {
    return static_cast<uint32_t>(static_cast<int32_t>(std::floor(v * 4096.0f)));
}

// Deterministic 3D hash -> [0,1].
inline float hash31(float x, float y, float z) {
    uint32_t h = mix32(noise_key(x) + mix32(noise_key(y) + mix32(noise_key(z))));
    return (h >> 8) * (1.0f / 16777216.0f);
}

// Deterministic 1D hash -> [0,1].
inline float hash11(float x) {
    return (mix32(noise_key(x)) >> 8) * (1.0f / 16777216.0f);
}

// Smooth 3D value noise in [0,1] (trilinear interpolation of lattice hashes).
// Sampled in object space, so it has no seam and no pole pinching on a sphere.
inline float value_noise3(Vec3 p) {
    float xi = std::floor(p.x), yi = std::floor(p.y), zi = std::floor(p.z);
    float xf = p.x - xi, yf = p.y - yi, zf = p.z - zi;
    float sx = xf * xf * (3.0f - 2.0f * xf);
    float sy = yf * yf * (3.0f - 2.0f * yf);
    float sz = zf * zf * (3.0f - 2.0f * zf);
    auto corner = [&](float dx, float dy, float dz) {
        return hash31(xi + dx, yi + dy, zi + dz);
    };
    float x00 = corner(0, 0, 0) + (corner(1, 0, 0) - corner(0, 0, 0)) * sx;
    float x10 = corner(0, 1, 0) + (corner(1, 1, 0) - corner(0, 1, 0)) * sx;
    float x01 = corner(0, 0, 1) + (corner(1, 0, 1) - corner(0, 0, 1)) * sx;
    float x11 = corner(0, 1, 1) + (corner(1, 1, 1) - corner(0, 1, 1)) * sx;
    float y0 = x00 + (x10 - x00) * sy;
    float y1 = x01 + (x11 - x01) * sy;
    return y0 + (y1 - y0) * sz;
}

// Layered fbm in [0,1].
inline float fbm3(Vec3 p, int octaves) {
    float sum = 0.0f, amp = 0.5f, tot = 0.0f;
    for (int i = 0; i < std::max(1, octaves); ++i) {
        sum += amp * value_noise3(p);
        tot += amp;
        p = p * 2.0f;
        amp *= 0.5f;
    }
    return (tot > 0.0f) ? sum / tot : 0.0f;
}
