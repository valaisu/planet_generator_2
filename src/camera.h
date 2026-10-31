#pragma once

#include <cmath>

#include "vec3.h"
#include "ray.h"

// Result of projecting a world point to the image plane (Phase 7 overlays).
// (s, t) are in [0, 1] with bottom-left origin, matching get_ray. `depth` is the
// forward distance from the camera; depth <= 0 means the point is behind it.
struct Projection {
    float s, t;
    float depth;
};

// Phase 2 / 4.1: LookAt camera. `fov` is vertical field of view in degrees.
class Camera {
public:
    Camera(Vec3 lookfrom, Vec3 lookat, Vec3 vup, float fov, float aspect_ratio) {
        constexpr float PI = 3.14159265358979323846f;
        float theta = fov * PI / 180.0f;
        float viewport_height = 2.0f * std::tan(theta * 0.5f);
        float viewport_width = aspect_ratio * viewport_height;

        // Right-handed camera frame: w points back (away from lookat).
        w = normalize(lookfrom - lookat);
        u = normalize(cross(vup, w));
        v = cross(w, u);

        origin = lookfrom;
        horizontal = viewport_width * u;
        vertical = viewport_height * v;
        lower_left_corner = origin - horizontal * 0.5f - vertical * 0.5f - w;
    }

    // s, t in [0, 1] across the image plane (s = x, t = y, bottom-left origin).
    Ray get_ray(float s, float t) const {
        Vec3 dir = lower_left_corner + horizontal * s + vertical * t - origin;
        return Ray(origin, normalize(dir));
    }

    // World-space camera basis (right = screen +x, up = screen +y, forward = view dir).
    Vec3 right() const { return u; }
    Vec3 up() const { return v; }
    Vec3 forward() const { return Vec3(0, 0, 0) - w; }
    Vec3 eye() const { return origin; }

    // Inverse of get_ray: world point -> image-plane (s, t) + forward depth.
    // Used to draw selection outlines / orbit rings aligned with the raytrace.
    Projection project(Vec3 p) const {
        Vec3 rel = p - origin;
        float zc = -dot(rel, w);          // forward distance (w points backward)
        float xc = dot(rel, u);
        float yc = dot(rel, v);
        float vw = length(horizontal);
        float vh = length(vertical);
        float s = 0.5f + (xc / zc) / vw;
        float t = 0.5f + (yc / zc) / vh;
        return Projection{s, t, zc};
    }

private:
    Vec3 origin;
    Vec3 lower_left_corner;
    Vec3 horizontal;
    Vec3 vertical;
    Vec3 u, v, w;
};
