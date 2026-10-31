#include <algorithm>
#include <cmath>
#include <cstdint>

#include "noise.h"
#include "render.h"

namespace {
float fract(float x) { return x - std::floor(x); }
Vec3 vfract(Vec3 v) { return Vec3(fract(v.x), fract(v.y), fract(v.z)); }
Vec3 vfloor(Vec3 v) {
    return Vec3(std::floor(v.x), std::floor(v.y), std::floor(v.z));
}

// Dave Hoskins' hash33: deterministic vec3 -> vec3 in [0,1]^3, no trig.
Vec3 hash33(Vec3 p) {
    p = vfract(Vec3(p.x * 0.1031f, p.y * 0.1030f, p.z * 0.0973f));
    float d = dot(p, Vec3(p.y, p.x, p.z) + Vec3(33.33f, 33.33f, 33.33f));
    p = p + Vec3(d, d, d);
    return vfract(Vec3((p.x + p.y) * p.z, (p.x + p.x) * p.y, (p.y + p.z) * p.x));
}

// Trilinear value noise + a small fbm over a direction, for large-scale star-
// density regions (darker patches / a denser band, not true nebulae).
float hash1(Vec3 p) { return hash33(p).x; }
float vnoise(Vec3 p) {
    Vec3 i = vfloor(p);
    Vec3 f = p - i;
    Vec3 u(f.x * f.x * (3 - 2 * f.x), f.y * f.y * (3 - 2 * f.y), f.z * f.z * (3 - 2 * f.z));
    auto L = [](float a, float b, float t) { return a + (b - a) * t; };
    float x00 = L(hash1(i + Vec3(0,0,0)), hash1(i + Vec3(1,0,0)), u.x);
    float x10 = L(hash1(i + Vec3(0,1,0)), hash1(i + Vec3(1,1,0)), u.x);
    float x01 = L(hash1(i + Vec3(0,0,1)), hash1(i + Vec3(1,0,1)), u.x);
    float x11 = L(hash1(i + Vec3(0,1,1)), hash1(i + Vec3(1,1,1)), u.x);
    return L(L(x00, x10, u.y), L(x01, x11, u.y), u.z);
}
float fbm2(Vec3 p) {
    return 0.6667f * vnoise(p) + 0.3333f * vnoise(p * 2.03f + Vec3(11, 17, 23));
}
}  // namespace

bool hit_world(const World& world, const Ray& r, float t_min, float t_max,
               HitRecord& rec, bool shading) {
    bool hit_anything = false;
    float closest = t_max;
    HitRecord temp;
    for (const auto& obj : world) {
        if (obj->hit(r, t_min, closest, temp, shading)) {
            hit_anything = true;
            closest = temp.t;
            rec = temp;
        }
    }
    return hit_anything;
}

namespace {
float smoothstep01(float a, float b, float x) {
    float t = std::clamp((x - a) / (b - a), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}
}  // namespace

Vec3 atmosphere_glow(const Atmosphere& a, Vec3 normal, Vec3 view_dir, Vec3 to_light) {
    // Fresnel-like rim: bright at the silhouette (normal perpendicular to view),
    // zero face-on. `thickness` widens the rim by lowering the exponent.
    float ndv = std::max(0.0f, dot(normal, view_dir));
    float power = 1.0f + (1.0f - std::clamp(a.thickness, 0.0f, 1.0f)) * 6.0f;
    float rim = std::pow(1.0f - ndv, power);

    // Only the sun-lit side glows; fade through the terminator into the night side.
    float sun = dot(normal, to_light);
    float visibility = smoothstep01(-0.25f, 0.4f, sun);

    // Sunset tint peaks where the sun grazes (terminator, sun ~ 0).
    float sunset_w = std::clamp(1.0f - std::fabs(sun) * 3.0f, 0.0f, 1.0f);
    Vec3 col = a.color * (1.0f - sunset_w) + a.sunset * sunset_w;

    return col * (rim * visibility * a.intensity);
}

Vec3 background(const Ray& r, const Background& bg) {
    // Project the ray direction onto a 3D grid; each cell may hold one star.
    // Sampling by direction (not UV) avoids pole pinching.
    constexpr float FREQ = 60.0f;     // grid resolution; finer = smaller cells
    Vec3 dir = normalize(r.direction);
    // Seed offset must stay small: at offsets ~1e8 (large seeds) float ULP is ~8,
    // which quantizes the star grid into visible blocks. Hash into [0, 1024).
    uint32_t hs = mix32(static_cast<uint32_t>(bg.seed));
    Vec3 seed_off(static_cast<float>(hs & 1023u),
                  static_cast<float>((hs >> 10) & 1023u),
                  static_cast<float>((hs >> 20) & 1023u));
    Vec3 p = dir * FREQ + seed_off;
    Vec3 base = vfloor(p);

    // Large-scale regions modulate local star density (darker/denser areas).
    float region = fbm2(dir * std::max(0.05f, bg.region_scale) + seed_off * 0.01f);
    float dfactor = 1.0f + bg.region_strength * (region - 0.5f) * 2.0f;
    float region_hi = smoothstep01(0.55f, 1.0f, region);  // how deep into a dense region

    // Galactic band: a great-circle (the equator of band_normal). band_t is 1 at the
    // band center, falling to 0 at its edge.
    float band_t = 0.0f;
    if (bg.band_enabled) {
        float c = std::fabs(dot(dir, normalize(bg.band_normal)));  // 0 on the band, 1 at poles
        band_t = 1.0f - smoothstep01(0.0f, std::max(0.01f, bg.band_width), c);
    }

    // Stars cluster in dense regions and along the band; brighten them there too.
    float local_density = std::clamp(bg.density * dfactor
                                     + bg.region_star_boost * region_hi
                                     + bg.band_density * band_t, 0.0f, 1.0f);
    float bright_boost = 1.0f + bg.region_star_boost * region_hi + bg.band_density * band_t;
    float radius = std::max(0.01f, bg.size);

    // Scan the 3x3x3 neighborhood so a fully-jittered star near a cell edge isn't
    // clipped into an arc — the old single-cell sample produced visible grid rings.
    // Keep the brightest contributor so stars stay round points, not blobs.
    float best = 0.0f;
    Vec3 best_tint(0, 0, 0);
    for (int dz = -1; dz <= 1; ++dz)
        for (int dy = -1; dy <= 1; ++dy)
            for (int dx = -1; dx <= 1; ++dx) {
                Vec3 cell = base + Vec3(static_cast<float>(dx), static_cast<float>(dy),
                                        static_cast<float>(dz));
                Vec3 r1 = hash33(cell);
                if (r1.x >= local_density) continue;  // no star in this cell
                Vec3 r2 = hash33(cell + Vec3(13.1f, 47.3f, 7.7f));
                Vec3 star = cell + r2;  // full-cell jitter -> no lattice rows
                float dist = length(p - star);
                float core = std::max(0.0f, 1.0f - dist / radius);
                float intensity = core * core * bg.brightness * bright_boost * (0.4f + 0.6f * r1.y);
                if (intensity > best) {
                    best = intensity;
                    float cv = (r1.z - 0.5f) * 2.0f * bg.color_variation;
                    best_tint = Vec3(bg.tint.x + cv, bg.tint.y, bg.tint.z - cv);
                }
            }

    Vec3 col = bg.sky;
    if (bg.region_glow > 0.0f)  // faint additive haze in the densest regions
        col = col + bg.tint * (bg.region_glow * region_hi);
    if (bg.band_enabled && bg.band_glow > 0.0f)  // tinted haze along the galactic band
        col = col + bg.band_tint * (bg.band_glow * band_t);
    col = col + best_tint * best;
    return col;
}

Vec3 ray_color(const Ray& r, const World& world, const std::vector<Light>& lights,
               float ambient, bool shadows, const Background& bg) {
    constexpr float EPS = 1e-3f;       // shadow-acne offset

    HitRecord rec;
    if (!hit_world(world, r, EPS, 1e30f, rec))
        return background(r, bg);

    // Emissive surfaces ignore lighting/shadows (sun, self-lit bodies).
    if (rec.material->emissive)
        return rec.albedo;

    const Vec3 albedo = rec.albedo;
    Vec3 color = albedo * ambient;       // ambient floor so shadows aren't pure black
    const float scale = 1.0f - ambient;  // a single full-on light reaches full albedo

    for (const Light& l : lights) {
        Vec3 to_light;
        float max_dist;     // how far a shadow occluder counts
        float atten = 1.0f;
        if (l.directional) {
            to_light = normalize(-l.vec);  // l.vec is the travel direction
            max_dist = 1e30f;
        } else {
            Vec3 d = l.vec - rec.p;        // l.vec is the world position
            max_dist = length(d);
            to_light = (max_dist > 1e-6f) ? d / max_dist : Vec3(0, 1, 0);
            if (l.falloff) {
                // Soft inverse-square; the +1 keeps nearby lights from blowing out.
                atten = 1.0f / (1.0f + max_dist * max_dist);
            }
        }

        float diffuse = std::max(0.0f, dot(rec.normal, to_light));
        if (diffuse <= 0.0f) continue;

        if (shadows) {
            // Only opaque bodies cast shadows; emissive bodies (the suns, including
            // the light's own sphere at max_dist) are transparent to shadow rays.
            Ray shadow(rec.p + rec.normal * EPS, to_light);
            HitRecord occ;
            if (hit_world(world, shadow, EPS, max_dist - EPS, occ, false) &&
                !occ.material->emissive)
                continue;
        }

        // Tint the albedo by the light color (component-wise), then weight it.
        Vec3 lit(albedo.x * l.color.x, albedo.y * l.color.y, albedo.z * l.color.z);
        color = color + lit * (diffuse * atten * scale);
    }

    // Additive atmosphere limb glow, keyed off the main (first) light.
    if (rec.material->atmosphere.enabled && !lights.empty()) {
        Vec3 view_dir = normalize(-r.direction);
        const Light& main = lights[0];
        Vec3 to_light = main.directional ? normalize(-main.vec)
                                         : normalize(main.vec - rec.p);
        color = color + atmosphere_glow(rec.material->atmosphere, rec.normal,
                                        view_dir, to_light);
    }
    return color;
}
