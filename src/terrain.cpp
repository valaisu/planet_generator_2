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

}  // namespace

void generate_terrain(Planet& P, const PlanetParams& prm) {
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
