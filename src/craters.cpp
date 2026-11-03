// Impact craters on worlds without plate tectonics.
//
// Count from the derived crater density (craters > 1 km relative to the lunar
// maria, Neukum chronology: 4.8e-3 per km^2 at 3.5 Gyr), cumulative size
// distribution N(>D) ~ D^-1.8 (lunar, 1-100 km), from ~1.5 grid cells (about
// half a disk pixel: they read as texture) up to basins of ~0.25 R.
// Profiles in real km (Earth-equivalent metres when stamped): small craters are
// bowls 0.2 D deep, bigger ones shallower (depth ~ D^0.3) with flat terraced
// floors and central peaks (pits in volatile-rich ground), peak rings above
// ~120 km, shallow basins inside rugged massif rings above ~400 km (rarer: the
// distribution steepens above 0.25 R). Outlines are irregular (polygonal and
// scalloped when big); rims uneven; raised rims fall off as r^-3 outside. Older
// craters are shallower (degraded), big ones much more so (old basins relax),
// filled and worn on worlds with air (`crater_fill`), and are stamped first so
// younger ones cut them. Resurfaced plains keep few. The youngest big craters get
// bright ejecta and ray systems; on volatile-rich ground young ones get lobate
// (rampart) ejecta; on dusty worlds dark sand on the downwind floor.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <random>

#include "noise.h"
#include "planet.h"

namespace {
float smooth01(float a, float b, float x) {
    float t = std::clamp((x - a) / (b - a), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

struct Crater {
    Vec3 c;
    float rad;     // radius, radians
    float d_km;    // diameter
    float age;     // 0 = newest, 1 = as old as the surface
};
}  // namespace

void add_craters(Planet& P, const PlanetParams& prm) {
    const CubeGrid& g = P.grid;
    const int N = g.size();
    const WorldSpec& W = P.world;
    const float R = W.radius_km;
    const float to_equiv = 1000.0f * EARTH_RADIUS_KM / R;  // km real -> Earth-equivalent m
    std::mt19937 rng(prm.seed * 2246822519u + 5u);
    std::uniform_real_distribution<float> U(0.0f, 1.0f);
    std::normal_distribution<float> nd(0.0f, 1.0f);

    const double area = 4.0 * 3.14159265 * R * R;
    const double n1 = std::min(W.crater_density, 10.0f) * 4.8e-3 * area;
    // The population is drawn at the default grid's resolution (REF_SPACING = the
    // 256-grid cell) so every grid shows the same planet; finer grids only add
    // the craters too small for that grid, from their own random stream.
    const float REF_SPACING = 0.00570852729f;
    const float d_min = 1.5f * REF_SPACING * R, d_max = 0.7f * R, d_basin = 0.25f * R;
    const int count = static_cast<int>(std::min(40000.0, n1 * std::pow(d_min, -1.8)));
    const float d_fine = std::min(d_min, 1.5f * g.spacing() * R);
    const int fine_count = static_cast<int>(std::min(300000.0, n1 * (std::pow(d_fine, -1.8) - std::pow(d_min, -1.8))));
    if (P.resurfaced.empty()) P.resurfaced.assign(N, 0.0f);
    P.ray.assign(N, 0.0f);
    if (W.streaks > 0.0f) P.streak.assign(N, 0.0f);

    std::vector<Crater> cr;
    auto draw = [&](std::mt19937& r, int n, float lo, float hi) {
        for (int i = 0; i < n; ++i) {
            float dk = lo * std::pow(std::max(1e-6f, U(r)), -1.0f / 1.8f);
            if (dk > hi) continue;
            // The size distribution steepens for basins (few above ~0.25 R; Hellas ~0.7 R).
            if (dk > d_basin && U(r) > (d_basin / dk) * (d_basin / dk)) continue;
            Vec3 c = normalize(Vec3(nd(r), nd(r), nd(r)));
            float age = U(r);
            // Resurfaced plains keep only craters younger than themselves.
            if (age > W.crater_density_young / std::max(W.crater_density, 1e-6f) && U(r) < g.sample(P.resurfaced, c)) continue;
            cr.push_back({c, 0.5f * dk / R, dk, age});
        }
    };
    draw(rng, count, d_min, d_max);
    std::mt19937 fine_rng(prm.seed * 2246822519u + 6u);
    draw(fine_rng, fine_count, d_fine, d_min);
    std::sort(cr.begin(), cr.end(), [](const Crater& a, const Crater& b) { return a.age > b.age; });
    if (std::getenv("CRATER_DEBUG")) {
        int rayed = 0;
        for (const Crater& C : cr) rayed += C.age < W.fresh && C.d_km > 2.0f * REF_SPACING * R;
        std::fprintf(stderr, "craters: %zu stamped (of %d drawn), %d rayed, D >= %.0f km\n", cr.size(), count + fine_count, rayed, d_fine);
    }

    if (W.dust > 0.0f) P.crater_sand.assign(N, 0.0f);
    std::vector<int> stamp(N, -1), queue;
    for (size_t k = 0; k < cr.size(); ++k) {
        const Crater& C = cr[k];
        const float dk = C.d_km;
        float depth = dk < 15.0f ? 0.2f * dk : 3.0f * std::pow(dk / 15.0f, 0.3f);
        const float depth0 = depth * W.crater_relief;  // fresh depth (basin massifs)
        depth *= 0.35f + 0.65f * (1.0f - C.age);  // degraded with age
        const float depth_rim = depth * W.crater_relief;  // rims outlast the floors' relaxation
        depth *= std::min(1.0f, std::pow(60.0f / dk, 0.6f) * (1.2f - C.age));  // big + old: relaxed
        depth *= W.crater_relief;
        // Air and water fill old craters (sediment, dust; up to `crater_fill` x
        // 1.5 km on the oldest) and wear their rims down: flat floors that bury
        // peaks and terraces; small old craters fill up into rim-only ghosts.
        const float fill = std::clamp(W.crater_fill * C.age * 1.5f / std::max(depth, 0.05f), 0.0f, 1.0f);
        const float rim = 0.3f * depth_rim * (1.0f - 0.7f * fill);
        const float cell_km = REF_SPACING * R;  // rays/streaks by size, not by grid
        // The youngest `fresh` share of craters is still bright (ejecta not yet
        // space-weathered, ~1 Gyr): a halo on every one, rays on those above ~2
        // cells, fading with age. Small craters dominate, so this is speckle.
        const bool fresh = C.age < W.fresh, rayed = fresh && dk > 2.0f * cell_km && W.dust <= 0.0f;  // dust blankets rays
        // Morphology by size: bowl -> complex (flat floor, terraced wall, central
        // peak or, in volatile-rich ground, a pit) -> peak ring -> multi-ring basin.
        const bool complex = dk > 20.0f, ring = dk > 120.0f, basin = dk > 400.0f;
        const bool pit = W.ejecta_lobes > 0.0f && hash11(5.3f * k + 0.7f) < 0.4f;
        const float floor_r = 0.35f + 0.25f * smooth01(20.0f, 200.0f, dk);
        const float terr = 0.8f * (1.0f - C.age) * smooth01(25.0f, 60.0f, dk);
        // Outline: no crater is a perfect circle; big ones are polygonal and
        // scalloped by wall slumps (azimuthal noise, stronger with size).
        const float irr = 0.06f + 0.25f * smooth01(15.0f, 300.0f, dk), scallop = smooth01(30.0f, 150.0f, dk);
        // Lobate ejecta (rampart craters): volatile-rich ground flows out as a
        // blanket with a lobed edge and a raised rim, while the crater is young.
        const float lobes = W.ejecta_lobes * smooth01(0.6f, 0.2f, C.age) * smooth01(4.0f, 8.0f, dk) * smooth01(100.0f, 60.0f, dk);
        const float fade = fresh ? 1.0f - C.age / W.fresh : 0.0f;
        Vec3 e, n;
        CubeGrid::tangent_basis(C.c, e, n);
        // Prevailing surface wind (dusty air): trades inside the Hadley cell
        // (easterly, equatorward), westerlies beyond. A dust tail in the crater's
        // lee (4-8 radii long, narrowing) and dark sand on the downwind floor.
        Vec3 wdir(0, 0, 0);
        if (W.dust > 0.0f || W.streaks > 0.0f) {
            float lat = std::asin(std::clamp(C.c.y, -1.0f, 1.0f)) * 57.2958f, hadley = W.jet_lat / 1.5f;
            float ns = lat >= 0.0f ? 1.0f : -1.0f;
            wdir = std::fabs(lat) < hadley ? normalize(e * -1.0f + n * (-0.4f * ns)) : normalize(e + n * (0.2f * ns));
        }
        const bool streaked = W.streaks > 0.0f && dk > 2.0f * cell_km;
        const float tail = streaked ? 4.0f + 4.0f * hash11(13.7f * k) : 0.0f;
        const float sand = W.dust > 0.0f && dk > 1.5f * cell_km ? hash11(3.1f * k + 0.2f) * hash11(3.1f * k + 0.2f) * (1.0f - 0.5f * fill) : 0.0f;
        const float reach = std::max(rayed ? 10.0f : 3.0f, tail + 1.0f);  // in crater radii
        const Vec3 roff(13.1f * k, 7.7f, 3.3f);
        // Flood outward from the centre cell over the neighbour graph.
        queue.assign(1, g.cell_of(C.c));
        stamp[queue[0]] = static_cast<int>(k);
        for (size_t qi = 0; qi < queue.size(); ++qi) {
            int c = queue[qi];
            Vec3 d = g.dir(c);
            float r = std::acos(std::clamp(dot(d, C.c), -1.0f, 1.0f)) / C.rad;
            if (r > reach) continue;
            float h = 0.0f;
            if (r < 3.0f) {
                const float th = std::atan2(dot(d, n), dot(d, e));
                const Vec3 circ(std::cos(th), std::sin(th), 0.0f);
                float az = fbm3(circ * 1.6f + roff, 3) - 0.5f, sc = fbm3(circ * 6.0f + roff * 1.7f, 2) - 0.5f;
                r /= 1.0f + irr * (2.5f * az + 1.2f * scallop * sc);
                float rim_k = rim * std::clamp(1.0f + 2.5f * (fbm3(circ * 3.0f + roff * 0.6f, 2) - 0.5f), 0.3f, 1.5f);  // uneven, breached
                if (r < 1.0f) {
                    if (basin) {  // shallow, filled, inside a rugged ring of massifs
                        h = -depth * (1.0f - smooth01(0.55f, 1.0f, r)) + rim_k * smooth01(0.8f, 1.0f, r);
                    } else if (complex) {
                        float t = smooth01(floor_r, 1.0f, r);
                        float q = t * 3.0f, step = (std::floor(q) + smooth01(0.25f, 0.75f, q - std::floor(q))) / 3.0f;
                        t += terr * (step - t);
                        h = -0.85f * depth + (0.85f * depth + rim_k) * t;
                        if (ring) {
                            float broken = smooth01(0.35f, 0.6f, fbm3(circ * 4.0f + roff * 2.3f, 2));
                            h += 0.3f * depth * broken * std::exp(-(r - 0.5f) * (r - 0.5f) / (0.06f * 0.06f));
                        } else if (pit) {
                            h -= 0.15f * depth * smooth01(0.2f, 0.08f, r);
                        } else {
                            float pk = 0.10f + 0.06f * smooth01(20.0f, 120.0f, dk);
                            h += 0.4f * depth * std::exp(-(r * r) / (pk * pk)) * (0.6f + 0.8f * fbm3r(d * (2.0f / C.rad) + roff, 2));
                        }
                    } else {
                        h = -depth + (depth + rim_k) * r * r;
                    }
                    h = std::max(h, -depth * (1.0f - fill));  // infill: flat floor over everything
                } else {
                    h = rim_k / (r * r * r);
                    if (lobes > 0.0f) {
                        float edge = 1.9f + 3.0f * (fbm3(circ * 3.5f + roff * 3.1f, 3) - 0.5f);
                        float u = (r - 1.0f) / (edge - 1.0f);
                        if (u < 1.2f) h += lobes * 0.08f * depth * (0.6f * std::max(0.0f, 1.0f - u) + std::exp(-(u - 1.0f) * (u - 1.0f) / 0.012f));
                    }
                }
                if (basin) {  // rugged massif rings (main rim + a faint outer ring)
                    float mass = 1.0f - std::fabs(2.0f * fbm3r(d * (6.0f / C.rad) + roff, 3) - 1.0f);
                    float rings = std::exp(-(r - 1.0f) * (r - 1.0f) / (0.12f * 0.12f)) + 0.4f * std::exp(-(r - 1.6f) * (r - 1.6f) / (0.1f * 0.1f));
                    h += 0.15f * depth0 * (1.0f - 0.5f * C.age) * rings * mass * mass;
                }
                if (sand > 0.0f && r < 1.0f) {  // dark dunes on the downwind side of the floor
                    Vec3 rel = (d - C.c) / C.rad - wdir * 0.35f;
                    float s2 = sand * std::exp(-dot(rel, rel) / (0.35f * 0.35f)) * smooth01(1.0f, 0.7f, r);
                    P.crater_sand[c] = std::max(P.crater_sand[c], s2);
                }
            }
            P.elevation[c] += h * to_equiv;
            if (streaked) {
                Vec3 rel = (d - C.c) / C.rad;
                float a = dot(rel, wdir), x = dot(rel, cross(C.c, wdir));
                if (a > 0.5f && a < tail) {  // soft start and tail; overlaps add up (no hard wedge edges)
                    float u = a / tail, wdt = 1.0f - 0.5f * u;
                    P.streak[c] += smooth01(0.5f, 1.3f, a) * (1.0f - u) * (1.0f - u) * std::exp(-(x * x) / (wdt * wdt));
                }
            }
            if (r > (rayed ? 10.0f : 3.0f)) {  // streak-only reach: no relief out here
                for (int nb : g.nbr(c))
                    if (stamp[nb] != static_cast<int>(k)) { stamp[nb] = static_cast<int>(k); queue.push_back(nb); }
                continue;
            }
            if (fresh) {
                // Bright floor + ejecta blanket, then (big enough) narrow rays
                // fading out to ~10 radii (azimuthal noise sharpened into streaks).
                float halo = r < 1.0f ? 0.8f : std::exp(-(r - 1.0f) * (r - 1.0f) / 0.3f);
                float streak = 0.0f;
                if (rayed && r > 1.0f) {
                    float th = std::atan2(dot(d, n), dot(d, e));
                    streak = fbm3(Vec3(std::cos(th), std::sin(th), 0.0f) * 9.0f + roff, 3);
                    streak = smooth01(0.55f, 0.72f, streak) * std::exp(-(r - 1.0f) / 3.0f);
                }
                P.ray[c] = std::max(P.ray[c], fade * std::min(1.0f, halo + streak));
            }
            for (int nb : g.nbr(c))
                if (stamp[nb] != static_cast<int>(k)) { stamp[nb] = static_cast<int>(k); queue.push_back(nb); }
        }
    }
    for (float& v : P.streak) v = 1.0f - std::exp(-v);
    if (std::getenv("CRATER_DEBUG")) {
        int lit = 0;
        for (float r : P.ray) lit += r > 0.3f;
        std::fprintf(stderr, "craters: %.2f%% of cells in rays\n", 100.0 * lit / N);
    }
    if (!W.ocean) {  // crater floors must stay "land" (> 0) on a world without a sea
        float lo = *std::min_element(P.elevation.begin(), P.elevation.end());
        for (float& h : P.elevation) h -= lo - 1.0f;
    }
}

bool set_world_knobs(WorldSpec& w, const std::string& spec) {
    struct Knob { const char* name; float* v; };
    const Knob knobs[] = {
        {"relief", &w.relief}, {"volcanism", &w.volcanism}, {"crater_density", &w.crater_density}, {"crater_density_young", &w.crater_density_young},
        {"crater_relief", &w.crater_relief}, {"dark_amount", &w.dark_amount}, {"stain_amount", &w.stain_amount},
        {"slope_albedo", &w.slope_albedo}, {"crest_albedo", &w.crest_albedo}, {"mottle", &w.mottle},
        {"roughness", &w.roughness}, {"scarps", &w.scarps}, {"streaks", &w.streaks}, {"ice_below", &w.ice_below}, {"frost_lat", &w.frost_lat},
        {"fresh", &w.fresh}, {"scarp_height", &w.scarp_height}, {"streak_strength", &w.streak_strength}, {"shading", &w.shading},
        {"alt_amount", &w.alt_amount}, {"auto_p95", &w.auto_p95}, {"deck_asym", &w.deck_asym}, {"deck_texture", &w.deck_texture}, {"lommel_seeliger", &w.lommel_seeliger}, {"saturation", &w.saturation}, {"edge", &w.edge}, {"lineae", &w.lineae}, {"lineae_width", &w.lineae_width}, {"grain", &w.grain}, {"exposure", &w.exposure},
        {"dark_mix", &w.dark_mix}, {"dark_level", &w.dark_level}, {"dust", &w.dust}, {"dust_elev", &w.dust_elev}, {"sand_lows", &w.sand_lows}, {"canyons", &w.canyons}, {"dust_steps", &w.dust_steps}, {"dust_lift", &w.dust_lift}, {"dust_octaves", &w.dust_octaves}, {"dust_window", &w.dust_window}, {"dust_texture", &w.dust_texture}, {"dust_soft", &w.dust_soft}, {"relief_light", &w.relief_light}, {"relief_sun", &w.relief_sun}, {"crater_fill", &w.crater_fill}, {"ejecta_lobes", &w.ejecta_lobes}, {"ground_detail", &w.ground_detail},
    };
    size_t pos = 0;
    while (pos < spec.size()) {
        size_t end = spec.find(',', pos);
        if (end == std::string::npos) end = spec.size();
        std::string item = spec.substr(pos, end - pos);
        size_t eq = item.find('=');
        bool ok = false;
        for (const Knob& k : knobs)
            if (eq != std::string::npos && item.compare(0, eq, k.name) == 0 && std::strlen(k.name) == eq) {
                *k.v = std::strtof(item.c_str() + eq + 1, nullptr);
                ok = true;
            }
        if (!ok) {
            std::fprintf(stderr, "unknown --world knob '%s' (have:", item.c_str());
            for (const Knob& k : knobs) std::fprintf(stderr, " %s", k.name);
            std::fprintf(stderr, ")\n");
            return false;
        }
        pos = end + 1;
    }
    return true;
}
