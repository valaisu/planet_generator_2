#pragma once

#include "vec3.h"
#include "ray.h"
#include "material.h"

struct HitRecord {
    float t = 0.0f;
    Vec3 p;          // intersection point
    Vec3 normal;     // outward surface normal at p
    // Referenced, not copied. `albedo` is the final surface color (after any
    // surface function), which is what shading reads.
    const Material* material = nullptr;
    Vec3 albedo;
};

class Hittable {
public:
    virtual ~Hittable() = default;

    // True + fills `rec` if the ray hits within (t_min, t_max). With shading=false
    // (shadow rays) the surface color is skipped — only geometry + material.
    virtual bool hit(const Ray& r, float t_min, float t_max, HitRecord& rec,
                     bool shading = true) const = 0;

    Vec3 center;
};
