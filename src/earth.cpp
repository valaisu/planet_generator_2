#include <cmath>

#include "earth.h"

Camera earth_camera(float yaw_deg) {
    // Distance so the unit sphere's silhouette fills PLANET_FRAC of the frame:
    // tan(angular radius) = PLANET_FRAC * tan(fov/2), sin(angular radius) = 1/d.
    constexpr float PI = 3.14159265358979323846f;
    float t = PLANET_FRAC * std::tan(CAMERA_FOV * 0.5f * PI / 180.0f);
    float d = 1.0f / std::sin(std::atan(t));
    Vec3 eye = rotate_about(Vec3(0, 0, d), Vec3(0, 1, 0), -yaw_deg * PI / 180.0f);
    return Camera(eye, Vec3(0, 0, 0), Vec3(0, 1, 0), CAMERA_FOV, 1.0f);
}

Background earth_background() {
    // Space_sceneries' default sky (minus shooting stars — renders are stills).
    Background bg;
    bg.density = 0.63f;
    bg.brightness = 1.09f;
    bg.region_scale = 2.05f;
    bg.region_glow = 0.11f;
    bg.region_star_boost = 0.77f;
    bg.band_enabled = true;
    bg.band_normal = Vec3(0.4f, 1.25f, -0.1f);
    bg.band_width = 0.36f;
    bg.band_density = 0.79f;
    bg.band_glow = 0.12f;
    return bg;
}

namespace {
// Atmosphere at scale 1, in planet radii. Real Earth's (8 km scale height) would
// be under a pixel at 128 px, so it is ~6x thicker, with the scattering
// coefficients scaled down to keep Earth's vertical optical depth (Rayleigh ~0.27
// in blue). `scale` multiplies heights and divides coefficients.
constexpr float ATMO_TOP = 0.06f;  // shell thickness
constexpr float H_RAYLEIGH = 0.008f, H_MIE = 0.004f;
const Vec3 BETA_R(5.8f, 13.5f, 33.1f);
constexpr float BETA_M = 10.0f, MIE_G = 0.76f;
constexpr float SKY_INTENSITY = 10.0f;

// Ray-sphere (origin-centred) entry/exit distances; false on a miss.
bool sphere_span(Vec3 o, Vec3 d, float rad, float& t0, float& t1) {
    float b = dot(o, d), c = dot(o, o) - rad * rad;
    float disc = b * b - c;
    if (disc < 0.0f) return false;
    float s = std::sqrt(disc);
    t0 = -b - s;
    t1 = -b + s;
    return true;
}

struct Atmo { float top, hr, hm; };

// Rayleigh / Mie optical depth from p toward the sun; false if the planet blocks it.
bool sun_depth(const Atmo& A, Vec3 p, Vec3 to_sun, float& tr, float& tm) {
    float a0, a1;
    if (sphere_span(p, to_sun, 1.0f, a0, a1) && a1 > 0.0f && a0 > 1e-6f) return false;
    if (!sphere_span(p, to_sun, A.top, a0, a1)) { tr = tm = 0.0f; return true; }
    constexpr int NS = 6;
    float ds = a1 / NS;
    tr = tm = 0.0f;
    for (int i = 0; i < NS; ++i) {
        float h = length(p + to_sun * ((i + 0.5f) * ds)) - 1.0f;
        tr += std::exp(-h / A.hr) * ds;
        tm += std::exp(-h / A.hm) * ds;
    }
    return true;
}

Vec3 vexp(Vec3 v) { return Vec3(std::exp(v.x), std::exp(v.y), std::exp(v.z)); }
Vec3 vmul(Vec3 a, Vec3 b) { return Vec3(a.x * b.x, a.y * b.y, a.z * b.z); }
}  // namespace

Vec3 atmosphere(Vec3 o, Vec3 d, Vec3 base, float t_surface, Vec3 to_sun, Vec3 sun_color,
                float scale, float haze) {
    const Atmo A{1.0f + ATMO_TOP * scale, H_RAYLEIGH * scale, H_MIE * scale};
    const Vec3 br = BETA_R / scale;
    const float bm = BETA_M / scale;
    float t0, t1;
    if (!sphere_span(o, d, A.top, t0, t1) || t1 <= 0.0f) return base;
    t0 = std::max(t0, 0.0f);
    bool surface = t_surface > 0.0f;
    if (surface) t1 = t_surface;

    const int NV = scale < 0.5f ? 32 : 16;  // thin shells need finer steps
    float ds = (t1 - t0) / NV;
    float vr = 0.0f, vm = 0.0f;  // optical depth camera -> sample
    Vec3 sum_r(0, 0, 0), sum_m(0, 0, 0);
    for (int i = 0; i < NV; ++i) {
        Vec3 p = o + d * (t0 + (i + 0.5f) * ds);
        float h = length(p) - 1.0f;
        float rr = std::exp(-h / A.hr) * ds, rm = std::exp(-h / A.hm) * ds;
        vr += 0.5f * rr;
        vm += 0.5f * rm;
        float sr, sm;
        if (sun_depth(A, p, to_sun, sr, sm)) {
            Vec3 att = vexp((br * (vr + sr) + Vec3(1, 1, 1) * (bm * 1.1f * (vm + sm))) * -1.0f);
            sum_r = sum_r + att * rr;
            sum_m = sum_m + att * rm;
        }
        vr += 0.5f * rr;
        vm += 0.5f * rm;
    }
    constexpr float PI = 3.14159265358979323846f;
    float mu = dot(d, to_sun);
    float phase_r = 3.0f / (16.0f * PI) * (1.0f + mu * mu);
    float g2 = MIE_G * MIE_G;
    float phase_m = 3.0f / (8.0f * PI) * ((1.0f - g2) * (1.0f + mu * mu))
                    / ((2.0f + g2) * std::pow(1.0f + g2 - 2.0f * MIE_G * mu, 1.5f));
    Vec3 inscatter = vmul(sun_color, vmul(br, sum_r) * phase_r + sum_m * (bm * phase_m)) * SKY_INTENSITY;

    Vec3 view_t = vexp((br * vr + Vec3(1, 1, 1) * (bm * 1.1f * vm)) * -1.0f);
    if (surface) {
        // Stylised: halve the haze looking straight down (it washed out land
        // colour), rising back to full strength toward the limb.
        Vec3 ground = o + d * t1, up = normalize(ground);
        float down = std::max(0.0f, dot(d * -1.0f, up));
        inscatter = inscatter * ((1.0f - 0.5f * down * down) * haze);
        // Sunlight reaching the ground is reddened by its slant path. Only the
        // excess over an overhead sun's path counts: skylight (not modelled)
        // gives back the blue a high sun loses, so the lit disk stays neutral
        // and just the terminator goes orange.
        float sr, sm, zr, zm;
        Vec3 g0 = up * (1.0f + 1e-4f * scale);
        if (sun_depth(A, g0, to_sun, sr, sm) && sun_depth(A, g0, up, zr, zm))
            base = vmul(base, vexp((br * std::max(0.0f, sr - zr)
                                    + Vec3(1, 1, 1) * (bm * 1.1f * std::max(0.0f, sm - zm))) * -1.0f));
    }
    return vmul(base, view_t) + inscatter;
}

Ray pixel_ray(const Camera& cam, float x, float y, int w, int h) {
    float s = x / w;
    float t = 1.0f - y / h;  // get_ray is bottom-left origin
    return cam.get_ray(s, t);
}
