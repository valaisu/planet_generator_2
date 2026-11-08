// Terrain: continents + static plates -> elevation (metres, sea level 0).
//
// Imitates the *outcome* of plate tectonics rather than simulating it (Frozen
// Fractal, "Around the World" parts 1-2):
//   1. Warped Voronoi regions are the building blocks.
//   2. Continents grow region by region from a few seeds until they cover the
//      land target, mostly kept apart by ocean -> non-convex, Earth-like shapes.
//   3. Plates grow independently over the same regions (so plate boundaries also
//      cut through continents), each rotating about a random Euler pole.
//   4. Each boundary is classified by crust types + relative motion and stamps
//      its landform as a function of distance from the boundary, on its own side:
//      collision ranges, Andean coastal ranges, trenches, island arcs, mid-ocean
//      ridges, rifts. Noise segments every belt along its length.
//   5. Crust base heights + shelves + noise; sea level set by area percentile.
#include <algorithm>
#include <cmath>
#include <numeric>
#include <random>

#include "noise.h"
#include "planet.h"

namespace {

float smooth01(float a, float b, float x) {
    float t = std::clamp((x - a) / (b - a), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

Vec3 noise_vec(Vec3 p, int oct) {
    return Vec3(fbm3(p, oct), fbm3(p + Vec3(31.4f, 7.1f, 19.3f), oct),
                fbm3(p + Vec3(-11.7f, 23.9f, 5.5f), oct)) - Vec3(0.5f, 0.5f, 0.5f);
}

// Ridged fbm in [0,1]: sharp crests, for mountain texture.
float ridged(Vec3 p, int oct) {
    float sum = 0.0f, amp = 0.5f, tot = 0.0f;
    for (int i = 0; i < oct; ++i) {
        float n = 1.0f - std::fabs(2.0f * value_noise3(p) - 1.0f);
        sum += amp * n * n;
        tot += amp;
        p = p * 2.03f;
        amp *= 0.5f;
    }
    return sum / tot;
}

Vec3 random_dir(std::mt19937& rng) {
    std::normal_distribution<float> nd(0.0f, 1.0f);
    return normalize(Vec3(nd(rng), nd(rng), nd(rng)));
}

uint32_t hash_u(uint32_t x) { return mix32(x * 0x9E3779B9u + 0x7F4A7C15u); }

// Tectonic lineaments on rocky worlds without plates: discrete, thin features
// rather than noise. A cooling planet contracts, so old crust breaks along lobate
// scarps (Mercury): long gently curving thrust fronts, a sharp step up on one side
// and a slope back down behind it; young volcanic plains buckle into lower,
// sinuous wrinkle ridges. Lengths are Pareto-distributed (many short, few long);
// most are a pixel or two wide and show only in the relief shading.
void add_lineaments(Planet& P, std::mt19937& rng, float RMAX) {
    const CubeGrid& g = P.grid;
    const int N = g.size();
    std::uniform_real_distribution<float> U(0.0f, 1.0f);
    std::normal_distribution<float> nd(0.0f, 1.0f);
    struct Lin { Vec3 c, along, across; float half_len, width, height, curve; bool wrinkle; };
    std::vector<Lin> lins;
    const int n = static_cast<int>(120 * P.world.scarps);
    for (int i = 0; i < n; ++i) {
        Vec3 c = normalize(Vec3(nd(rng), nd(rng), nd(rng)));
        Vec3 e, nn;
        CubeGrid::tangent_basis(c, e, nn);
        float a = U(rng) * 6.2832f;
        Vec3 along = e * std::cos(a) + nn * std::sin(a);
        bool wrinkle = g.sample(P.resurfaced, c) > 0.5f;
        float len = std::min(0.4f, 0.04f * std::pow(std::max(1e-3f, U(rng)), -0.6f));  // radians
        float side = U(rng) < 0.5f ? -1.0f : 1.0f;
        lins.push_back({c, along, cross(c, along) * side, 0.5f * len, wrinkle ? 0.006f : 0.004f,
                        RMAX * (wrinkle ? 0.008f : 0.02f) * P.world.scarp_height * (0.5f + U(rng)), 3.0f * nd(rng), wrinkle});
    }
    #pragma omp parallel for schedule(dynamic, 256)
    for (int c = 0; c < N; ++c) {
        Vec3 d = g.dir(c);
        float dh = 0.0f;
        for (const Lin& L : lins) {
            float cd = dot(d, L.c);
            if (cd < std::cos(L.half_len + 0.05f)) continue;
            float u = dot(d, L.along) / L.half_len;           // -1..1 along
            if (std::fabs(u) > 1.0f) continue;
            float v = dot(d, L.across) - L.curve * L.half_len * u * u * 0.1f;  // gentle bow
            float x = v / L.width;
            float taper = std::sqrt(std::max(0.0f, 1.0f - u * u));
            float prof = L.wrinkle ? std::exp(-x * x) * (1.0f + 0.3f * std::sin(9.0f * u))   // low sinuous ridge
                                   : smooth01(-1.0f, 0.0f, x) * std::exp(-std::max(0.0f, x) / 4.0f);  // thrust step
            dh += L.height * taper * prof;
        }
        P.elevation[c] += dh;
    }
}

// Worlds without plate tectonics. Heights are fractions of the derived maximum
// relief (Earth-equivalent metres, see WorldSpec), so gravity and size set them.
//  - Stagnant lid (Mars, Venus): a hemispheric dichotomy (one side sunk into
//    smooth lowlands), a volcanic rise carrying most of the big shield
//    volcanoes, the rest scattered; more volcanism = more, lower shields.
//  - Dead (Mercury): gentle undulation with patches of smooth plains; the
//    craters (add_craters) make the rest.
//  - Ice shell (Europa, Pluto): low relief; a tidally flexed shell is cut by
//    lineae (warped great-circle arcs, double ridges); volatile ice fills one
//    big young basin ringed by blocky mountains (Sputnik Planitia).
// `resurfaced` marks young plains (fewer craters, ice-filled basin).
void generate_other_terrain(Planet& P, const PlanetParams& prm) {
    const CubeGrid& g = P.grid;
    const int N = g.size();
    const WorldSpec& W = P.world;
    std::mt19937 rng(prm.seed);
    std::uniform_real_distribution<float> U(0.0f, 1.0f);
    const Vec3 off(U(rng) * 100.0f, U(rng) * 100.0f, U(rng) * 100.0f);
    const float RMAX = 20000.0f * W.relief;
    P.elevation.assign(N, 0.0f);
    P.resurfaced.assign(N, 0.0f);
    P.crack.assign(N, 0.0f);

    auto angle = [](Vec3 a, Vec3 b) { return std::acos(std::clamp(dot(a, b), -1.0f, 1.0f)); };
    struct Shield { Vec3 c; float r, h; };
    std::vector<Shield> shields;
    struct Canyon { Vec3 s, t; float len, width, depth, wiggle, phase; };  // great-circle trough from s along t
    std::vector<Canyon> canyons;
    Vec3 axis = random_dir(rng), rise = random_dir(rng), basin = random_dir(rng);
    if (W.style == TerrainStyle::Stagnant && W.volcanism > 0.01f) {
        // The rise sits near the dichotomy boundary (Tharsis).
        rise = normalize(cross(axis, random_dir(rng)) + axis * 0.2f);
        int n = std::clamp(static_cast<int>(std::lround(3.0f + 10.0f * W.volcanism)), 3, 14);
        for (int i = 0; i < n; ++i) {
            Vec3 c = i < (n + 1) / 2 ? normalize(rise + random_dir(rng) * 0.35f) : random_dir(rng);
            float h = RMAX * 0.35f * (0.4f + 0.6f * U(rng)) / (1.0f + 2.0f * W.volcanism) * (i == 0 ? 1.0f / 0.7f : 0.7f);
            shields.push_back({c, 0.07f + 0.10f * U(rng), h});
        }
        // The rise's load cracks the crust radially: one great canyon system
        // (deep trough + side canyons, Valles Marineris) and thin shallow grabens.
        if (W.canyons > 0.0f) {
            auto radial = [&](float start, float len, float width, float depth, float wiggle) {
                Vec3 dir = random_dir(rng);
                dir = normalize(dir - rise * dot(dir, rise));  // tangent at the rise
                Vec3 s0 = normalize(rotate_about(rise, cross(rise, dir), start));
                Vec3 t0 = normalize(dir - s0 * dot(dir, s0));
                canyons.push_back({s0, t0, len, width, depth, wiggle, U(rng) * 6.28f});
            };
            radial(0.25f, 0.9f, 0.035f, 0.16f * RMAX * W.canyons, 0.015f);
            const Canyon main_c = canyons.back();
            for (int i = 0; i < 6; ++i) {  // side canyons branching off the main trough
                float at = (0.15f + 0.7f * U(rng)) * main_c.len, side = U(rng) < 0.5f ? -1.0f : 1.0f;
                Vec3 p = normalize(main_c.s * std::cos(at) + main_c.t * std::sin(at));
                Vec3 tan = normalize(main_c.t * std::cos(at) - main_c.s * std::sin(at));
                Vec3 nrm = cross(p, tan) * side;
                Vec3 t1 = normalize(tan * 0.5f + nrm);
                canyons.push_back({p, t1, 0.08f + 0.12f * U(rng), 0.010f, 0.10f * RMAX * W.canyons, 0.008f, U(rng) * 6.28f});
            }
            for (int i = 0; i < 14; ++i) radial(0.15f + 0.2f * U(rng), 0.3f + 0.5f * U(rng), 0.005f, 0.015f * RMAX * W.canyons, 0.004f);
        }
    }
    if (W.glacier_basin) basin = normalize(Vec3(basin.x, 0.4f * basin.y, basin.z));  // low latitudes
    struct Arc { Vec3 pole, centre; float cos_len, width, strength; };
    std::vector<Arc> arcs;
    if (W.cracks)
        // A dense network at all lengths (many short, few long; Pareto), mostly
        // thin, a few broad bands: Europa is criss-crossed at every scale.
        for (int i = 0; i < static_cast<int>(1200 * W.lineae); ++i)
            arcs.push_back({random_dir(rng), random_dir(rng), std::cos(std::min(1.2f, 0.05f * std::pow(std::max(1e-3f, U(rng)), -0.7f))),
                            (0.0045f + 0.008f * U(rng) * U(rng) * U(rng)) * W.lineae_width, 0.4f + 0.6f * U(rng)});

    #pragma omp parallel for schedule(static)
    for (int c = 0; c < N; ++c) {
        Vec3 d = g.dir(c);
        // Large undulation (+ optional grid-scale roughness: it reads as noise).
        float h = RMAX * (0.05f * (2.0f * fbm3(d * 1.5f + off, 4) - 1.0f) + 0.02f * (2.0f * fbm3(d * 7.0f + off * 1.3f, 4) - 1.0f)
                          + 0.012f * W.roughness * (ridged(d * 30.0f + off * 2.1f, 3) - 0.5f));
        float young = 0.0f;
        if (W.style == TerrainStyle::Stagnant) {
            float s = dot(normalize(d + noise_vec(d * 1.8f + off, 3) * 0.6f), axis);
            float low = smooth01(-0.05f, 0.45f, s);
            h += -0.10f * RMAX * low;
            young = 0.97f * low;  // the lowlands are volcanic plains: their craters are erased
            if (W.ground_detail > 0.0f) {
                // Ground between the craters: old crust is knobby and rough (hills
                // at ~90 km, battered by impacts); the boundary scarp breaks up
                // into flat-topped mesas and knobs (fretted terrain); young plains
                // carry wrinkle ridges (sinuous compressional ridges, noise contours).
                Vec3 q = normalize(d + noise_vec(d * 6.0f + off * 1.9f, 3) * 0.05f);
                // (a finer ~40 km octave doubled the 1-px detail vs the photos: left out)
                float k1 = 1.0f - std::fabs(2.0f * fbm3r(q * 40.0f + off * 2.3f, 3) - 1.0f);
                h += W.ground_detail * RMAX * (1.0f - low) * 0.025f * k1 * k1;
                float edge = 4.0f * low * (1.0f - low);
                float mesa = smooth01(0.56f, 0.6f, fbm3r(q * 30.0f + off * 4.1f, 3));
                h += W.ground_detail * RMAX * 0.03f * edge * mesa;
                float wr = (fbm3r(q * 9.0f + off * 5.3f, 4) - 0.5f) / 0.012f;
                h += W.ground_detail * RMAX * 0.015f * low * std::exp(-wr * wr);
            }
            float ar = angle(d, rise);
            if (!shields.empty()) {
                float bulge = std::exp(-(ar * ar) / (0.35f * 0.35f));
                h += 0.08f * RMAX * bulge;
                young = std::max(young, 0.7f * bulge);
            }
            for (const Canyon& cn : canyons) {
                float along = std::atan2(dot(d, cn.t), dot(d, cn.s));
                if (along < 0.0f || along > cn.len) continue;
                float x = std::asin(std::clamp(dot(d, cross(cn.s, cn.t)), -1.0f, 1.0f))
                          + cn.wiggle * std::sin(along * 25.0f + cn.phase) + 0.5f * cn.wiggle * std::sin(along * 61.0f + 2.0f * cn.phase);
                float u = along / cn.len, taper = std::sin(3.14159f * std::min(1.0f, u * 1.4f)) * (u < 0.7f ? 1.0f : 1.0f - (u - 0.7f) / 0.3f * 0.6f);
                float w = cn.width * (0.6f + 0.6f * taper);
                h -= cn.depth * taper * smooth01(w, 0.5f * w, std::fabs(x));  // steep walls, flat floor
                young = std::max(young, 0.0f);
            }
            for (const Shield& sh : shields) {
                float r = angle(d, sh.c) / sh.r;
                if (r >= 1.0f) continue;
                float cone = sh.h * std::pow(1.0f - r, 1.5f);
                if (r < 0.08f) cone -= 0.12f * sh.h * (1.0f - r / 0.08f);  // caldera
                h += cone;
                young = std::max(young, 1.0f - r);
            }
        } else if (W.style == TerrainStyle::Dead) {
            young = 0.8f * smooth01(0.55f, 0.65f, fbm3(d * 1.5f + off * 0.7f, 4));
            h -= 0.02f * RMAX * young;
        } else if (W.style == TerrainStyle::IceShell) {
            h *= 0.5f;
            if (W.cracks) {
                young = 1.0f;
                Vec3 q = normalize(d + noise_vec(d * 3.0f + off, 3) * 0.08f);  // curving lineae
                float cr = 0.0f;
                for (const Arc& a : arcs) {
                    if (dot(q, a.centre) < a.cos_len) continue;
                    float x = dot(q, a.pole) / a.width;
                    cr = std::max(cr, a.strength * std::exp(-x * x));
                }
                P.crack[c] = cr;
                h += 0.002f * RMAX * cr;
            }
            if (W.glacier_basin) {
                float r = angle(d, basin) / 0.45f;
                float floor = smooth01(1.0f, 0.85f, r);
                float ring = std::exp(-(r - 1.05f) * (r - 1.05f) / 0.02f);
                h = h * (1.0f - floor) - 0.08f * RMAX * floor
                    + 0.12f * RMAX * ring * ridged(d * 9.0f + off, 4);
                young = std::max(young, floor);
            }
        }
        P.elevation[c] = h;
        P.resurfaced[c] = std::clamp(young, 0.0f, 1.0f);
    }
    if (W.scarps > 0.0f) add_lineaments(P, rng, RMAX);
}

}  // namespace

void generate_terrain(Planet& P, const PlanetParams& prm) {
    if (P.world.style != TerrainStyle::Plates) {
        generate_other_terrain(P, prm);
        // No liquid: everything is "land" (elevation > 0); else sea level by area.
        float z0 = P.world.ocean ? area_percentile(P.grid, P.elevation, 1.0f - prm.land_fraction)
                                 : *std::min_element(P.elevation.begin(), P.elevation.end()) - 1.0f;
        for (float& h : P.elevation) h -= z0;
        return;
    }
    const CubeGrid& g = P.grid;
    const int N = g.size();
    std::mt19937 rng(prm.seed);
    std::uniform_real_distribution<float> U(0.0f, 1.0f);
    const Vec3 off(U(rng) * 100.0f, U(rng) * 100.0f, U(rng) * 100.0f);

    // --- 1. Warped Voronoi regions --------------------------------------------
    const int R = prm.regions;
    std::vector<Vec3> seeds(R);
    for (Vec3& s : seeds) s = random_dir(rng);
    P.region.assign(N, 0);
    #pragma omp parallel for schedule(static)
    for (int c = 0; c < N; ++c) {
        Vec3 d = g.dir(c);
        // Two-octave warp: long meanders plus shorter wiggles on region edges.
        Vec3 w = normalize(d + noise_vec(d * 1.3f + off, 3) * 0.55f
                             + noise_vec(d * 4.0f + off, 3) * 0.18f);
        int best = 0;
        float bd = -2.0f;
        for (int r = 0; r < R; ++r) {
            float dd = dot(w, seeds[r]);
            if (dd > bd) { bd = dd; best = r; }
        }
        P.region[c] = best;
    }
    std::vector<std::vector<int>> adj(R);
    std::vector<double> rarea(R, 0.0);
    for (int c = 0; c < N; ++c) {
        int a = P.region[c];
        rarea[a] += g.area(c);
        for (int k = 0; k < 4; ++k) {
            int b = P.region[g.nbr(c)[k]];
            if (b != a && std::find(adj[a].begin(), adj[a].end(), b) == adj[a].end())
                adj[a].push_back(b);
        }
    }

    // --- 2. Continents grown from regions -------------------------------------
    // Crust target a bit above the land target: shelves end up submerged.
    const double crust_target = prm.land_fraction + 0.05;
    const int C = prm.continents;
    std::vector<int> cont(R, -1);
    std::vector<int> cseed;
    for (int tries = 0; static_cast<int>(cseed.size()) < C && tries < 1000; ++tries) {
        int r = static_cast<int>(U(rng) * R) % R;
        bool ok = cont[r] < 0;
        for (int s : cseed) ok = ok && dot(seeds[s], seeds[r]) < std::cos(0.7f);
        if (ok) { cont[r] = static_cast<int>(cseed.size()); cseed.push_back(r); }
    }
    // Lognormal size shares: a couple of big continents, some small ones.
    std::vector<double> share(cseed.size()), carea(cseed.size(), 0.0);
    double ssum = 0.0;
    std::lognormal_distribution<float> ln(0.0f, 0.7f);
    for (size_t k = 0; k < cseed.size(); ++k) { share[k] = ln(rng); ssum += share[k]; }
    double total = 0.0;
    for (size_t k = 0; k < cseed.size(); ++k) {
        share[k] = share[k] / ssum * crust_target;
        carea[k] = rarea[cseed[k]];
        total += carea[k];
    }
    std::vector<bool> stuck(cseed.size(), false);
    while (total < crust_target) {
        // Grow the continent furthest below its share.
        int k = -1;
        double worst = -1e9;
        for (size_t i = 0; i < cseed.size(); ++i) {
            double deficit = (share[i] - carea[i]) / share[i] + 0.3 * U(rng);
            if (!stuck[i] && deficit > worst) { worst = deficit; k = static_cast<int>(i); }
        }
        if (k < 0) break;
        std::vector<int> frontier;
        for (int r = 0; r < R; ++r) {
            if (cont[r] >= 0) continue;
            bool touches = false, foreign = false;
            for (int b : adj[r]) {
                if (cont[b] == k) touches = true;
                else if (cont[b] >= 0) foreign = true;
            }
            // Keep continents apart, but let a few merge (Eurasia + Africa).
            if (touches && (!foreign || U(rng) < 0.03f)) frontier.push_back(r);
        }
        if (frontier.empty()) { stuck[k] = true; continue; }
        int r = frontier[static_cast<size_t>(U(rng) * frontier.size()) % frontier.size()];
        cont[r] = k;
        carea[k] += rarea[r];
        total += rarea[r];
    }
    // Fill ocean regions boxed in by crust: the keep-apart rule leaves one-region
    // gaps that flood into narrow channels through land, lined with coastal
    // ranges. Wider seas (several ocean regions) keep their open neighbours.
    for (int pass = 0; pass < 2; ++pass) {
        std::vector<int> nc = cont;
        for (int r = 0; r < R; ++r) {
            if (cont[r] >= 0 || adj[r].empty()) continue;
            int land = 0, k = -1;
            for (int b : adj[r])
                if (cont[b] >= 0) { ++land; k = cont[b]; }
            if (4 * land >= 3 * static_cast<int>(adj[r].size())) nc[r] = k;
        }
        cont.swap(nc);
    }

    // --- 3. Plates (independent of continents) --------------------------------
    const int NP = prm.plates;
    std::vector<int> rplate(R, -1);
    std::vector<std::pair<int, int>> front;  // (region, plate)
    for (int p = 0; p < NP; ++p) {
        int r = static_cast<int>(U(rng) * R) % R;
        if (rplate[r] < 0) front.push_back({r, p});
    }
    while (!front.empty()) {
        size_t i = static_cast<size_t>(U(rng) * front.size()) % front.size();
        auto [r, p] = front[i];
        front[i] = front.back();
        front.pop_back();
        if (rplate[r] >= 0) continue;
        rplate[r] = p;
        for (int b : adj[r])
            if (rplate[b] < 0) front.push_back({b, p});
    }
    std::vector<Vec3> pole(NP);
    std::vector<float> omega(NP);
    for (int p = 0; p < NP; ++p) {
        pole[p] = random_dir(rng);
        omega[p] = 0.4f + 0.8f * U(rng);
    }

    P.plate.resize(N);
    P.continental.resize(N);
    P.plate_velocity.resize(N);
    for (int c = 0; c < N; ++c) {
        int p = std::max(0, rplate[P.region[c]]);
        P.plate[c] = p;
        P.continental[c] = cont[P.region[c]] >= 0;
        P.plate_velocity[c] = cross(pole[p], g.dir(c)) * omega[p];
    }

    // --- 4. Boundaries: classify + distance on each side ----------------------
    P.convergence.assign(N, 0.0f);
    std::vector<uint8_t> other_cont(N, 0);
    std::vector<int> other_plate(N, -1), bsrc;
    for (int c = 0; c < N; ++c) {
        float conv = 0.0f;
        int cnt = 0, oc = 0;
        for (int k = 0; k < 4; ++k) {
            int nb = g.nbr(c)[k];
            if (P.plate[nb] == P.plate[c]) continue;
            Vec3 dn = normalize(g.dir(nb) - g.dir(c));
            conv += dot(P.plate_velocity[c] - P.plate_velocity[nb], dn);
            oc += P.continental[nb];
            other_plate[c] = P.plate[nb];
            ++cnt;
        }
        if (!cnt) continue;
        P.convergence[c] = conv / cnt;
        other_cont[c] = 2 * oc >= cnt;
        bsrc.push_back(c);
    }
    // The per-cell boundary normal follows the grid's staircase, so raw
    // convergence flickers cell to cell; diffuse it along the boundary.
    for (int it = 0; it < 12; ++it) {
        std::vector<float> nc(P.convergence);
        for (int c : bsrc) {
            float sum = P.convergence[c];
            int cnt = 1;
            for (int nb : g.nbr(c))
                if (P.convergence[nb] != 0.0f && P.plate[nb] == P.plate[c]) {
                    sum += P.convergence[nb];
                    ++cnt;
                }
            nc[c] = sum / cnt;
        }
        P.convergence.swap(nc);
    }
    std::vector<int> nearest_b;
    std::vector<float> bdist = distance_from(g, bsrc, &nearest_b,
        [&](int a, int b) { return P.plate[a] == P.plate[b]; });

    // Signed distance to the continental crust edge (km): >0 inland, <0 offshore.
    std::vector<int> csrc, osrc;
    for (int c = 0; c < N; ++c) (P.continental[c] ? csrc : osrc).push_back(c);
    std::vector<float> d_to_cont = distance_from(g, csrc);
    std::vector<float> d_to_ocean = distance_from(g, osrc);

    // --- 5. Elevation ----------------------------------------------------------
    const float KM = EARTH_RADIUS_KM;
    P.elevation.resize(N);
    #pragma omp parallel for schedule(static)
    for (int c = 0; c < N; ++c) {
        Vec3 d = g.dir(c);
        bool cc = P.continental[c];
        float inland = cc ? d_to_ocean[c] * KM : -d_to_cont[c] * KM;

        // Crust base: low coastal plains rising to interior plateaus; offshore a
        // shelf of varying width, then the continental slope to the abyss.
        float h;
        if (cc) {
            h = 150.0f + 650.0f * (1.0f - std::exp(-inland / 700.0f))
                + 900.0f * (2.0f * fbm3(d * 1.8f + off * 0.4f, 3) - 1.0f);  // highlands/basins
        } else {
            float sn = fbm3(d * 3.0f + off * 1.3f, 3);
            float shelf = 30.0f + 600.0f * sn * sn;
            float out = -inland;
            h = (out < shelf) ? -40.0f - 160.0f * out / shelf
                              : -200.0f - 4000.0f * smooth01(0.0f, 450.0f, out - shelf);
        }

        // Boundary landforms, on this cell's own side of the nearest boundary.
        float mountain = 0.0f;
        int b = nearest_b[c];
        float dk = bdist[c] * KM;
        if (b >= 0 && dk < 2000.0f) {
            float cv = P.convergence[b];
            bool self_c = P.continental[b], oth_c = other_cont[b];
            // Noise segments each belt along its length (no uniform walls).
            float seg = std::clamp(1.6f * fbm3(d * 3.5f + off * 0.7f, 3) - 0.25f, 0.0f, 1.2f);
            if (cv > 0.1f) {
                float s = std::min(cv, 1.5f);
                if (self_c && oth_c) {               // collision: broad ranges + plateau
                    float w = 400.0f + 300.0f * s;
                    mountain += 3800.0f * s * seg * std::exp(-(dk * dk) / (w * w));
                } else if (self_c) {                 // overriding continent: Andes
                    float x = (dk - 250.0f) / 220.0f;
                    mountain += 3200.0f * s * seg * std::exp(-x * x);
                } else {
                    bool over = !oth_c && hash_u(P.plate[c]) > hash_u(other_plate[b]);
                    if (over) {                      // ocean-ocean overriding: island arc
                        float x = (dk - 150.0f) / 90.0f;
                        mountain += 5200.0f * s * seg * std::exp(-x * x);
                    } else {                         // subducting: trench
                        h -= 2000.0f * s * std::exp(-(dk * dk) / (90.0f * 90.0f));
                    }
                }
            } else if (cv < -0.1f) {
                float s = std::min(-cv, 1.5f);
                if (self_c) {                        // continental rift: broad valley + shoulders
                    // East-African style: stays mostly above sea level (a narrow
                    // deep trough floods into an unnatural channel through land).
                    float x = (dk - 260.0f) / 140.0f;
                    h += -350.0f * s * std::exp(-(dk * dk) / (200.0f * 200.0f))
                         + 450.0f * s * seg * std::exp(-x * x);
                } else {                             // mid-ocean ridge
                    h += 1800.0f * s * std::exp(-(dk * dk) / (300.0f * 300.0f));
                }
            }
        }
        h += mountain * (0.5f + 0.9f * ridged(d * 6.0f + off, 5));

        // Coastline noise: big low-frequency relief near the crust edge makes
        // peninsulas, bays and offshore islands; fades toward interiors/abyss.
        float coast_w = std::exp(-std::fabs(inland) / 500.0f);
        h += 1100.0f * coast_w * (2.0f * fbm3(d * 4.0f + off * 2.1f, 5) - 1.0f);
        // General detail: hills on land, abyssal roughness at sea.
        h += (cc ? 420.0f : 250.0f) * (2.0f * fbm3(d * 10.0f + off * 3.3f, 5) - 1.0f);
        P.elevation[c] = h;
    }

    // Sea level: exactly land_fraction of the surface ends up above 0.
    float z0 = area_percentile(g, P.elevation, 1.0f - prm.land_fraction);
    for (float& h : P.elevation) h -= z0;
}
