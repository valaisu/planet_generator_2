#pragma once

#include "vec3.h"

// Phase 1 / 3.2: Ray definition.
struct Ray {
    Vec3 origin;
    Vec3 direction;

    Ray() = default;
    Ray(Vec3 origin, Vec3 direction) : origin(origin), direction(direction) {}

    Vec3 point_at_parameter(float t) const { return origin + direction * t; }
};
