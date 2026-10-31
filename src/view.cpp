#include <cmath>
#include <random>

#include "earth.h"
#include "view.h"

namespace {
constexpr float PI = 3.14159265358979323846f;
constexpr float DEG = PI / 180.0f;

Vec3 from_latlon(float lat, float lon) {
    return Vec3(std::cos(lat * DEG) * std::sin(lon * DEG), std::sin(lat * DEG),
                std::cos(lat * DEG) * std::cos(lon * DEG));
}

// Mean cloud opacity over a ~1000 km patch around d (13 samples).
float cloud_around(const Planet& P, Vec3 d) {
    Vec3 e, n;
    CubeGrid::tangent_basis(d, e, n);
    float sum = P.grid.sample(P.cloud, d);
    for (int k = 0; k < 12; ++k) {
        float a = k * PI / 6.0f, r = (k % 2 ? 0.08f : 0.04f);
        sum += P.grid.sample(P.cloud, normalize(d + (e * std::cos(a) + n * std::sin(a)) * r));
    }
    return sum / 13.0f;
}

// A land cell within two cells of the sea, |lat| 5..55 deg — a coastline is the
// most informative thing to look at from orbit. Partly cloudy if possible
// (like the ISS photos), falling back to any coast.
Vec3 pick_coast(const Planet& P, std::mt19937& rng) {
    const CubeGrid& g = P.grid;
    std::uniform_int_distribution<int> U(0, g.size() - 1);
    for (int tries = 0; tries < 200000; ++tries) {
        int c = U(rng);
        float alat = std::fabs(std::asin(g.dir(c).y)) / DEG;
        if (P.elevation[c] <= 0.0f || alat < 5.0f || alat > 55.0f) continue;
        if (tries < 100000) {
            float cl = cloud_around(P, g.dir(c));
            if (cl < 0.12f || cl > 0.4f) continue;
        }
        for (int nb : g.nbr(c))
            for (int nb2 : g.nbr(nb))
                if (P.elevation[nb2] <= 0.0f) return g.dir(c);
    }
    return Vec3(0, 0, 1);
}
}  // namespace

View disk_view(int size, float yaw_deg, float sun_az_deg) {
    Vec3 to_sun = rotate_about(normalize(Vec3(0.65f, -0.30f, 0.70f)), Vec3(0, 1, 0), -(yaw_deg + sun_az_deg) * DEG);
    View v{earth_camera(yaw_deg), size, size, to_sun};
    v.pixel_km = 2.0f * EARTH_RADIUS_KM / (PLANET_FRAC * size);
    v.sun_color = v.sun_color * 1.15f;  // exposure, matched to earth_2 (tools/color_stats.py)
    // Real-thickness atmosphere over the planet: the ~6x exaggerated shell
    // greyed the whole disk. The exaggerated shell only draws the limb glow.
    v.atmo_scale = 0.16f;
    v.rim_scale = 1.0f;
    v.haze = 0.3f;  // earth_2 shows little haze; full haze outshone the dim terminator side
    return v;
}

View leo_view(const Planet& P, const LeoParams& prm, int width, int height, uint32_t seed) {
    std::mt19937 rng(seed * 2654435761u + 99u);
    std::uniform_real_distribution<float> U(0.0f, 1.0f);
    Vec3 target = (std::isnan(prm.lat) || std::isnan(prm.lon)) ? pick_coast(P, rng)
                                                               : from_latlon(prm.lat, prm.lon);
    float heading = std::isnan(prm.heading) ? U(rng) * 360.0f : prm.heading;

    Vec3 east, north;
    CubeGrid::tangent_basis(target, east, north);
    Vec3 fwd = north * std::cos(heading * DEG) + east * std::sin(heading * DEG);

    // Sun: sun_elev above the target's horizon, from the front-right.
    float az = (heading + 60.0f) * DEG, el = prm.sun_elev_deg * DEG;
    Vec3 to_sun = normalize(target * std::sin(el) + (north * std::cos(az) + east * std::sin(az)) * std::cos(el));

    // Fly-along: slide the look point (and heading) forward along the great circle.
    if (prm.along_km != 0.0f) {
        Vec3 ax = normalize(cross(target, fwd));
        float a = prm.along_km / EARTH_RADIUS_KM;
        target = normalize(rotate_about(target, ax, a));
        fwd = normalize(rotate_about(fwd, ax, a));
    }

    // Horizontal FOV ~64 deg; pitch the view so the horizon (dip acos(1/(1+h))
    // below horizontal) sits ~8 deg above centre, i.e. in the top quarter.
    const float alt = prm.alt_km / EARTH_RADIUS_KM;
    const float aspect = static_cast<float>(width) / height;
    const float vfov = 2.0f * std::atan(std::tan(32.0f * DEG) / aspect) / DEG;
    const float dip = std::acos(1.0f / (1.0f + alt)) / DEG;
    const float pitch = (dip + 0.25f * vfov) * DEG;

    // Place the camera back along -heading so the view centre lands on the target:
    // the centre ray leaves at `pitch` below horizontal and meets the sphere.
    float back = 0.0f;
    {
        Vec3 o(0, 1.0f + alt, 0), d(std::cos(pitch), -std::sin(pitch), 0);  // 2D slice
        float b = dot(o, d), c = dot(o, o) - 1.0f, disc = b * b - c;
        if (disc > 0.0f) {
            Vec3 hit = o + d * (-b - std::sqrt(disc));
            back = std::atan2(hit.x, hit.y);  // arc from nadir to the centre hit
        }
    }
    Vec3 axis = normalize(cross(target, fwd));         // rotating about it moves along fwd
    Vec3 nadir = rotate_about(target, axis, -back);
    Vec3 e2, n2;
    CubeGrid::tangent_basis(nadir, e2, n2);
    Vec3 fwd2 = normalize(fwd - nadir * dot(fwd, nadir));
    Vec3 eye = nadir * (1.0f + alt);
    Vec3 look = fwd2 * std::cos(pitch) - nadir * std::sin(pitch);

    View v{Camera(eye, eye + look, nadir, vfov, aspect), width, height, to_sun};
    v.pixel_rad = vfov * DEG / height;
    v.atmo_scale = 0.16f;  // real thickness (8 km scale height)
    v.sun_color = v.sun_color * 1.6f;
    v.haze = 0.55f;  // the ISS photos show clear ground below ~45 deg from nadir  // exposure: ISS photos are exposed for the ground
    v.relief = 8.0f;
    v.detail = true;
    return v;
}
