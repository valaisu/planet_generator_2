#pragma once

#include "vec3.h"

// Additive limb glow: bright at the sun-lit silhouette, tinted toward `sunset`
// near the terminator. No transparency, no second ray.
struct Atmosphere {
    bool enabled = false;
    Vec3 color{0.4f, 0.6f, 1.0f};   // day-side rim color (sky)
    Vec3 sunset{1.0f, 0.5f, 0.2f};  // tint near the terminator (sun grazing)
    float thickness = 0.4f;         // rim width: 0 = thin sliver .. 1 = broad glow
    float intensity = 1.0f;         // overall strength
};

struct Material {
    Vec3 albedo{0.5f, 0.5f, 0.5f};
    bool emissive = false;          // self-lit: ignores lighting, transparent to shadows
    Atmosphere atmosphere;
};
