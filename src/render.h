#pragma once

#include <algorithm>
#include <cstdint>
#include <memory>
#include <vector>

#include "vec3.h"
#include "ray.h"
#include "hittable.h"

// Shading core. A "world" is just a list of hittables.
using World = std::vector<std::shared_ptr<Hittable>>;

// Procedural starfield for rays that hit nothing. A seeded star function sampled
// by ray direction over `sky`. Stable per seed.
struct Background {
    Vec3 sky{0.01f, 0.01f, 0.02f};  // base near-black space color
    float density = 0.08f;          // probability a grid cell holds a star [0,1]
    float brightness = 1.0f;        // star intensity scale
    float size = 0.2f;              // star radius in grid-cell units (0..0.5)
    Vec3 tint{1.0f, 1.0f, 1.0f};    // base star color
    float color_variation = 0.25f;  // warm/cool spread per star
    int seed = 1337;

    // Large-scale density regions: a low-frequency field carves darker and denser
    // areas across the sky (no true nebulae). region_glow adds a faint haze in the
    // densest regions.
    float region_scale = 2.0f;      // region feature size (lower = broader patches)
    float region_strength = 0.5f;   // how strongly regions modulate local star density
    float region_glow = 0.0f;       // additive haze in dense regions (0 = off)
    float region_star_boost = 0.0f; // extra star density + brightness inside dense regions

    // Galactic band ("Milky Way"): a great-circle band — a non-noise pattern — that
    // concentrates stars and a tinted haze along the plane perpendicular to band_normal.
    bool band_enabled = false;
    Vec3 band_normal{0, 1, 0};       // pole of the band; the band is the equator of this
    float band_width = 0.25f;        // angular half-width of the band (|cos| from equator)
    float band_density = 0.5f;       // extra star density along the band center
    float band_glow = 0.06f;         // additive haze along the band (0 = off)
    Vec3 band_tint{0.80f, 0.85f, 1.0f};
};

// A single light: directional (the sun, far away) or positional.
struct Light {
    bool directional = true;  // true: `vec` is travel direction; false: `vec` is world position
    Vec3 vec{0, -1, -1};      // travel direction (directional) or world position (positional)
    Vec3 color{1, 1, 1};      // light color / intensity
    bool falloff = false;     // positional only: apply distance attenuation
};

// Nearest hit across the world within (t_min, t_max). Pass shading=false for
// occlusion/shadow rays to skip procedural surface texturing on each candidate.
bool hit_world(const World& world, const Ray& r, float t_min, float t_max,
               HitRecord& rec, bool shading = true);

// Additive atmosphere limb glow at a surface hit. `normal` is the
// outward surface normal, `view_dir` points from the surface toward the camera,
// `to_light` toward the main sun. Brightest at the sun-lit silhouette, tinted
// toward `sunset` near the terminator, zero on the night side. Exposed for tests.
Vec3 atmosphere_glow(const Atmosphere& a, Vec3 normal, Vec3 view_dir, Vec3 to_light);

// Color for a ray that hits nothing — the starfield.
Vec3 background(const Ray& r, const Background& bg = Background{});

// Shade a primary ray against a set of lights. Emissive
// surfaces return their albedo; others get ambient floor + summed diffuse, each
// light optionally shadow-tested. Misses return the starfield described by `bg`.
Vec3 ray_color(const Ray& r, const World& world, const std::vector<Light>& lights,
               float ambient, bool shadows, const Background& bg = Background{});

// Pack a linear color into 0xAABBGGRR (little-endian byte order R,G,B,A), clamped.
// Shared by every render entry so output layout stays consistent (row 0 = top).
inline uint32_t pack_rgba(Vec3 c) {
    auto to_byte = [](float v) {
        v = std::min(1.0f, std::max(0.0f, v));
        return static_cast<uint32_t>(v * 255.0f + 0.5f);
    };
    return to_byte(c.x) | (to_byte(c.y) << 8) | (to_byte(c.z) << 16) | (0xFFu << 24);
}
