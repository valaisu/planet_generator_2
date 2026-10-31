// Clouds from a weather simulation.
//
// 1. Flow: the barotropic vorticity equation on the sphere — the classic minimal
//    model of large-scale weather. Absolute vorticity q = zeta + f is carried by
//    the wind (semi-Lagrangian); the wind comes back from zeta through a
//    streamfunction (lap psi = zeta, multigrid on a cube-grid hierarchy;
//    u = n x grad psi). Zeta is gently relaxed toward an Earth-like 500 hPa
//    profile (westerly jets at ~45 deg, easterlies in the tropics). The jets are
//    barotropically unstable, so they meander and roll up into cyclones and
//    anticyclones by themselves; the beta effect (f varying with latitude) gives
//    Rossby waves. Units: planet radius 1, jet speed ~1, time unit ~2.5 days.
// 2. Clouds: a cloud-water tracer carried by that wind. Sources follow the
//    climate (humid/rainy air, storm tracks, cold-current decks) and rising air
//    in cyclones; it evaporates with a ~1-day lifetime, faster in anticyclones
//    (sinking air). Filaments, commas, spirals and frontal bands come from the
//    flow stretching and wrapping the tracer — nothing is drawn by hand.
// 3. Opacity from a bounded cascade (Cahalan 1994) on top of the tracer — see
//    the final section.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <random>

#include "noise.h"
#include "planet.h"

namespace {
constexpr float PI = 3.14159265358979323846f;
constexpr float OMEGA2 = 31.0f;  // 2 * Omega in model units (Earth with U=30 m/s)

float smooth01(float a, float b, float x) {
    float t = std::clamp((x - a) / (b - a), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}
float gauss(float x, float mu, float sig) { return std::exp(-0.5f * (x - mu) * (x - mu) / (sig * sig)); }
float lat_deg(Vec3 d) { return std::asin(std::clamp(d.y, -1.0f, 1.0f)) * 180.0f / PI; }

// Bounded cascade (see the final section of simulate_clouds). Fitted to the NASA
// map with tools/cloud_stats.py: opacity mix 46/30/14/10 % (clear/thin/medium/
// opaque; real 47/29/18/7), spectral slope -1.62 (real -1.66). F > 1 lets a step
// zero out a region -> clear holes, as real fields have. The 256-grid (~40 km
// cells) holds octaves below GRID_OCTAVES; finer ones are evaluated per pixel.
constexpr int OCTAVES = 8, ADVECTED = 4, GRID_OCTAVES = 6, ANIM_OCTAVES = 4;
constexpr float F_CASCADE = 1.2f, C_CASCADE = 0.84f, BASE_FREQ = 3.0f;

// Multiplicative step of octave n at direction d.
float cascade_step(Vec3 d, Vec3 off, int n) {
    float freq = BASE_FREQ * static_cast<float>(1 << n);
    float amp = F_CASCADE * std::pow(C_CASCADE, static_cast<float>(n));
    Vec3 p = d * freq + off + Vec3(17.3f, 5.1f, 9.7f) * static_cast<float>(n);
    // Value noise clusters around 0.5 (sd ~0.15); stretch it to ~+-1.
    float sn = std::clamp(3.0f * (value_noise3(p) - 0.5f), -1.0f, 1.0f);
    return std::max(0.0f, 1.0f + amp * sn);
}

// --- Poisson solver: lap psi = rhs on the sphere, multigrid over cube grids ------
// Discrete Laplacian: CubeGrid::quadratic_fit (least-squares quadratic fit); the
// wind uses the gradient from the same fit, so curl(wind) matches zeta.
// A fixed 9-point stencil was badly wrong across cube-face seams (error ~10x the
// value), so the wind didn't match the vorticity and the flow ran away.
struct Level {
    std::unique_ptr<CubeGrid> g;
    std::vector<CubeGrid::QuadFit> fit;  // gradient (for the wind) + Laplacian weights
    std::vector<std::array<float, 8>> w;
    std::vector<float> wsum;
    std::vector<int> coarse;  // cell in the next coarser level
};

struct Poisson {
    std::vector<Level> lv;

    explicit Poisson(const std::vector<int>& sizes) {
        for (int n : sizes) {
            Level L;
            L.g = std::make_unique<CubeGrid>(n);
            const CubeGrid& g = *L.g;
            L.w.resize(g.size());
            L.wsum.resize(g.size());
            L.fit.resize(g.size());
            for (int c = 0; c < g.size(); ++c) {
                g.quadratic_fit(c, L.fit[c]);
                L.w[c] = L.fit[c].lap;
                float s = 0.0f;
                for (float w : L.w[c]) s += w;
                L.wsum[c] = s;
            }
            lv.push_back(std::move(L));
        }
        for (size_t i = 0; i + 1 < lv.size(); ++i) {
            const CubeGrid& g = *lv[i].g;
            lv[i].coarse.resize(g.size());
            for (int c = 0; c < g.size(); ++c) lv[i].coarse[c] = lv[i + 1].g->cell_of(g.dir(c));
        }
    }

    void smooth(int l, std::vector<float>& psi, const std::vector<float>& rhs, int iters) {
        const Level& L = lv[l];
        std::vector<float> nx(psi.size());
        for (int it = 0; it < iters; ++it) {
            #pragma omp parallel for schedule(static)
            for (int c = 0; c < L.g->size(); ++c) {
                float s = 0.0f;
                for (int k = 0; k < 8; ++k) s += L.w[c][k] * psi[L.g->nbr(c)[k]];
                float j = (s - rhs[c]) / L.wsum[c];
                nx[c] = psi[c] + 0.8f * (j - psi[c]);  // weighted Jacobi
            }
            psi.swap(nx);
        }
    }

    void vcycle(int l, std::vector<float>& psi, const std::vector<float>& rhs) {
        const Level& L = lv[l];
        const CubeGrid& g = *L.g;
        if (l + 1 == static_cast<int>(lv.size())) { smooth(l, psi, rhs, 60); return; }
        smooth(l, psi, rhs, 3);
        // Residual, restricted (area-weighted mean) to the coarser grid.
        const CubeGrid& gc = *lv[l + 1].g;
        std::vector<float> res(g.size()), rc(gc.size(), 0.0f), ac(gc.size(), 0.0f);
        #pragma omp parallel for schedule(static)
        for (int c = 0; c < g.size(); ++c) {
            float lap = 0.0f;
            for (int k = 0; k < 8; ++k) lap += L.w[c][k] * (psi[g.nbr(c)[k]] - psi[c]);
            res[c] = rhs[c] - lap;
        }
        for (int c = 0; c < g.size(); ++c) {
            rc[L.coarse[c]] += res[c] * g.area(c);
            ac[L.coarse[c]] += g.area(c);
        }
        for (int c = 0; c < gc.size(); ++c) rc[c] = ac[c] > 0.0f ? rc[c] / ac[c] : 0.0f;
        std::vector<float> ec(gc.size(), 0.0f);
        vcycle(l + 1, ec, rc);
        #pragma omp parallel for schedule(static)
        for (int c = 0; c < g.size(); ++c) psi[c] += gc.sample(ec, g.dir(c));
        smooth(l, psi, rhs, 3);
    }

    // Solve on the finest level, warm-started from psi. Two V-cycles: with one,
    // the solve lags the vorticity and the flow destabilises.
    void solve(std::vector<float>& psi, std::vector<float> rhs) {
        const CubeGrid& g = *lv[0].g;
        double m = 0.0;  // lap psi integrates to 0 on a closed surface
        for (int c = 0; c < g.size(); ++c) m += rhs[c] * g.area(c);
        for (float& r : rhs) r -= static_cast<float>(m);
        for (int i = 0; i < 2; ++i) vcycle(0, psi, rhs);
        m = 0.0;
        for (int c = 0; c < g.size(); ++c) m += psi[c] * g.area(c);
        for (float& p : psi) p -= static_cast<float>(m);
    }
};

// Semi-Lagrangian departure point (midpoint rule) for velocity field u on g.
Vec3 depart(const CubeGrid& g, const std::vector<Vec3>& u, Vec3 p, float dt) {
    Vec3 mid = normalize(p - g.sample(u, p) * (0.5f * dt));
    return normalize(p - g.sample(u, mid) * dt);
}
}  // namespace

// The weather state, kept so an animation can keep the weather running.
struct CloudSim {
    float cloud_amount;
    Vec3 off;
    std::mt19937 rng;
    Poisson solver{{64, 32, 16, 8}};
    const CubeGrid& gw = *solver.lv[0].g;
    const int NW = gw.size();
    const CubeGrid gt{128};
    const int NT = gt.size();
    std::vector<Vec3> u_eq, u, u_prev, u_mid;
    std::vector<float> zeta_eq, zeta, zn, psi, f;
    std::vector<float> source, convect, cloud, cn;
    // Advected texture coordinates: where each cell's air was TEX_STEPS ago at
    // most. Noise evaluated there is stretched and folded by the simulated wind
    // at any resolution (only this smooth map is interpolated). The still image
    // uses one window (phase 0, age TEX_STEPS); animations cross-fade two phases
    // half a cycle apart, each restarting with fresh noise when its weight is 0,
    // so the patterns keep moving AND changing without ever over-stretching.
    std::vector<Vec3> origin[2], on;
    float age[2] = {-1.0f, -1.0f};  // model time since the phase started; < 0 = not started
    Vec3 phase_off[2];
    bool animating = false;
    int step_no = 0;
    float time = 0.0f, dt_prev = 0.0f;
    float time_per_minute = 0.0f;  // animations: model time per real minute (calibrated)

    CloudSim(const Planet& P, const PlanetParams& prm);
    void step(float dt, bool tracer, bool tex);
    void write(Planet& P);
};
// Model time unit ~2.5 days (radius 1, jet speed ~1 = 30 m/s): 1 minute = 1/3600.
constexpr float DT = 0.02f;        // ~1.2 h; ~1 flow cell per step at jet speed
constexpr int TEX_STEPS = 20;      // texture window ~0.4 time units (~1 day); longer over-stretches
constexpr float TEX_TIME = TEX_STEPS * DT;
constexpr int STEPS = 600;         // ~30 days: spin-up, then developed weather
constexpr float TAU_RELAX = 4.0f;  // jet relaxation (~10 days)
constexpr float TAU_CLOUD = 0.3f;  // cloud lifetime (~18 h)
constexpr float STIR = 80.0f;      // storm-track stirring strength

float coriolis(Vec3 d) { return OMEGA2 * d.y; }

CloudSim::CloudSim(const Planet& P, const PlanetParams& prm)
    : cloud_amount(prm.cloud_amount), rng(prm.seed * 7919u + 17u) {
    std::uniform_real_distribution<float> U(0.0f, 1.0f);
    off = Vec3(U(rng) * 50.0f, U(rng) * 50.0f, U(rng) * 50.0f);
    phase_off[0] = off;

    // Target 500 hPa-ish zonal wind: westerly jets ~45 deg (+ a weaker polar
    // jet), easterlies in the tropics.
    u_eq.resize(NW);
    for (int c = 0; c < NW; ++c) {
        Vec3 d = gw.dir(c), e, n;
        CubeGrid::tangent_basis(d, e, n);
        float a = std::fabs(lat_deg(d));
        float jet = 1.3f * gauss(a, 45.0f, 9.0f) + 0.4f * gauss(a, 65.0f, 8.0f);
        float trades = -0.35f * gauss(a, 0.0f, 15.0f);
        u_eq[c] = e * (jet + trades);
    }
    // Vorticity of a tangent field: curl u = -div(n x u).
    zeta_eq.resize(NW);
    {
        std::vector<Vec3> r(NW);
        for (int c = 0; c < NW; ++c) r[c] = cross(gw.dir(c), u_eq[c]);
        for (int c = 0; c < NW; ++c) zeta_eq[c] = -gw.divergence(r, c);
    }

    // Start from the target plus random vortices (the instability seeds).
    zeta.resize(NW);
    zn.resize(NW);
    psi.assign(NW, 0.0f);
    f.resize(NW);
    for (int c = 0; c < NW; ++c) {
        Vec3 d = gw.dir(c);
        f[c] = coriolis(d);
        zeta[c] = zeta_eq[c] + 6.0f * (fbm3(d * 4.0f + off, 3) - 0.5f);
    }
    u.resize(NW);
    u_prev.resize(NW);
    u_mid.resize(NW);

    // --- Cloud-water tracer (finer grid) ----------------------------------------
    source.resize(NT);
    convect.resize(NT);
    cloud.assign(NT, 0.0f);
    cn.resize(NT);
    origin[0].resize(NT);
    origin[1].resize(NT);
    on.resize(NT);
    for (int c = 0; c < NT; ++c) {
        Vec3 d = gt.dir(c);
        int k = P.grid.cell_of(d);
        float alat = std::fabs(lat_deg(d));
        bool sea = P.elevation[k] <= 0.0f;
        // Climatological cloudiness: humid/rainy air, storm tracks, cold-current
        // stratocumulus; little over the subtropical highs and dry land.
        float s = 0.55f * smooth01(0.25f, 0.85f, P.humidity[k])
                + 0.30f * smooth01(250.0f, 2200.0f, P.precip[k])
                + 0.15f * gauss(alat, 50.0f, 12.0f)
                + (sea ? 0.35f * smooth01(-1.0f, -4.0f, P.sst_anomaly[k]) : 0.0f)
                - 0.20f * gauss(alat, 22.0f, 7.0f);
        source[c] = std::max(0.02f, s) * (1.0f - 0.7f * smooth01(60.0f, 80.0f, alat));
        // Tropical convection: warm, rainy air rises on its own (no vortex needed).
        convect[c] = smooth01(18.0f, 27.0f, P.temperature[k]) * smooth01(800.0f, 2500.0f, P.precip[k]);
    }
}

// One weather step of dt (model time); `tracer` also steps the cloud water, `tex`
// the texture phases. Spin-up uses DT; animations smaller steps (minutes).
void CloudSim::step(float dt, bool tracer, bool tex) {
    const int s = step_no++;
    solver.solve(psi, zeta);
    #pragma omp parallel for schedule(static)
    for (int c = 0; c < NW; ++c) {
        const CubeGrid::QuadFit& F = solver.lv[0].fit[c];
        float gx = 0.0f, gy = 0.0f;
        for (int k = 0; k < 8; ++k) {
            float dp = psi[gw.nbr(c)[k]] - psi[c];
            gx += F.gx[k] * dp;
            gy += F.gy[k] * dp;
        }
        u[c] = cross(gw.dir(c), F.east * gx + F.north * gy);  // u = n x grad psi
    }
    // Advect with the wind extrapolated to mid-step (Adams-Bashforth 2; for equal
    // steps u_mid = 1.5 u - 0.5 u_prev). Using u^n alone is forward Euler on the
    // Rossby-wave dynamics, which amplifies them every step (~5%/step for the
    // fastest planetary waves).
    if (s == 0) { u_prev = u; dt_prev = dt; }
    const float ext = 0.5f * dt / dt_prev;
    #pragma omp parallel for schedule(static)
    for (int c = 0; c < NW; ++c) u_mid[c] = u[c] + (u[c] - u_prev[c]) * ext;
    u_prev = u;
    dt_prev = dt;

    if (std::getenv("WEATHER_DEBUG") && s % 50 == 0) {
        float um = 0, zm = 0, res = 0;
        const Level& L = solver.lv[0];
        for (int c = 0; c < NW; ++c) {
            um = std::max(um, length(u[c]));
            zm = std::max(zm, std::fabs(zeta[c]));
            float lap = 0;
            for (int k = 0; k < 8; ++k) lap += L.w[c][k] * (psi[gw.nbr(c)[k]] - psi[c]);
            res = std::max(res, std::fabs(lap - zeta[c]));
        }
        int cm = 0;
        float dm = 0, cz = 0;
        std::vector<Vec3> rot(NW);
        for (int c = 0; c < NW; ++c) rot[c] = cross(gw.dir(c), u[c]);
        for (int c = 0; c < NW; ++c) {
            if (length(u[c]) >= um) cm = c;
            dm = std::max(dm, std::fabs(gw.divergence(u, c)));
            cz = std::max(cz, std::fabs(-gw.divergence(rot, c) - zeta[c]));
        }
        std::fprintf(stderr, "step %d |u|max %.3g at lat %.1f face %d |zeta|max %.3g residual %.3g |div u| %.3g |curl u - zeta| %.3g\n",
                     s, um, lat_deg(gw.dir(cm)), cm / (64 * 64), zm, res, dm, cz);
    }
    // Advect absolute vorticity q = zeta + f, then relax toward the jets.
    // Only zeta goes through grid interpolation; the (much larger, smooth)
    // planetary part f is evaluated exactly at the departure point —
    // interpolating f near cube seams made errors proportional to the wind,
    // which fed back as spurious vorticity and blew the flow up.
    #pragma omp parallel for schedule(static)
    for (int c = 0; c < NW; ++c) {
        Vec3 dep = depart(gw, u_mid, gw.dir(c), dt);
        float z = gw.sample(zeta, dep) + coriolis(dep) - f[c];
        zn[c] = z + (zeta_eq[c] - z) * (dt / TAU_RELAX);
    }
    zeta.swap(zn);
    // Stochastic stirring in the storm tracks: a slowly evolving random
    // vorticity forcing. A one-layer model has no baroclinic instability (the
    // real source of mid-latitude storms), so this stands in for it; the flow
    // organises the stirring into eddies, vortices and meandering jets.
    {
        const Vec3 drift = off * 0.9f + Vec3(0.37f, 0.11f, -0.23f) * (time * 3.0f);
        #pragma omp parallel for schedule(static)
        for (int c = 0; c < NW; ++c) {
            Vec3 d = gw.dir(c);
            float band = gauss(std::fabs(lat_deg(d)), 48.0f, 14.0f) + 0.15f * gauss(std::fabs(lat_deg(d)), 15.0f, 8.0f);
            zeta[c] += dt * STIR * band * (2.0f * fbm3(d * 7.0f + drift, 3) - 1.0f);
        }
    }

    // Cloud tracer: advect, then source (more in cyclones = rising air) and
    // decay (faster in anticyclones = sinking air).
    if (tracer) {
        #pragma omp parallel for schedule(static)
        for (int c = 0; c < NT; ++c) {
            Vec3 d = gt.dir(c);
            float v = gt.sample(cloud, depart(gw, u_mid, d, dt));
            float cyc = gw.sample(zeta, d) * d.y / (std::fabs(d.y) + 0.2f);  // >0 cyclonic
            float lift = std::clamp(cyc / 4.0f, -1.0f, 2.0f);
            float extra = smooth01(12.0f, 28.0f, std::fabs(lat_deg(d)));  // extratropics
            float src = source[c] * (0.15f + 1.5f * std::max(0.0f, lift) * extra) + 0.8f * convect[c];
            float decay = (1.0f + std::max(0.0f, -lift)) / TAU_CLOUD;
            cn[c] = v + dt * (src - decay * v);
        }
        cloud.swap(cn);
    }
    if (tex) {
        std::uniform_real_distribution<float> U(0.0f, 1.0f);
        for (int ph = 0; ph < (animating ? 2 : 1); ++ph) {
            if (age[ph] < 0.0f || age[ph] >= 2.0f * TEX_TIME - 1e-4f) {  // (re)start this phase
                for (int c = 0; c < NT; ++c) origin[ph][c] = gt.dir(c);
                age[ph] = 0.0f;
                if (animating) phase_off[ph] = Vec3(U(rng) * 50.0f, U(rng) * 50.0f, U(rng) * 50.0f);
                continue;
            }
            #pragma omp parallel for schedule(static)
            for (int c = 0; c < NT; ++c) on[c] = gt.sample(origin[ph], depart(gw, u_mid, gt.dir(c), dt));
            origin[ph].swap(on);
            age[ph] += dt;
        }
    }
    time += dt;
}

// --- Final cloud opacity: bounded cascade (Cahalan 1994) ------------------------------
// Optical thickness = mean thickness from the weather tracer x a multiplicative
// cascade: prod_n max(0, 1 + F_CASCADE * C_CASCADE^n * s_n), s_n in [-1,1]
// noise at scale 2^-n. Gives the lognormal-like thickness distribution and
// ~k^-5/3 spectrum measured in real clouds; opacity = 1 - exp(-tau) gives
// natural translucency. The largest octaves sample at the flow-advected
// origin, so big structures follow the simulated weather (animations: all grid
// octaves, blended over the two phases). A global factor is solved so the mean
// opacity hits `cloud_amount` (Earth ~0.24, measured on the NASA map with
// tools/cloud_stats.py).
void CloudSim::write(Planet& P) {
    const CubeGrid& g = P.grid;
    float c95;
    {
        std::vector<float> tmp = cloud;
        std::nth_element(tmp.begin(), tmp.begin() + NT * 95 / 100, tmp.end());
        c95 = std::max(1e-6f, tmp[NT * 95 / 100]);
    }
    // Phase weights: a triangle over each phase's 2*TEX_STEPS life (sum = 1).
    float w[2] = {1.0f, 0.0f};
    if (animating)
        for (int ph = 0; ph < 2; ++ph) w[ph] = std::max(0.0f, 1.0f - std::fabs(age[ph] - TEX_TIME) / TEX_TIME);
    std::vector<float> tau(g.size()), fine(g.size());
    #pragma omp parallel for schedule(static)
    for (int c = 0; c < g.size(); ++c) {
        Vec3 d = g.dir(c);
        float cas = 0.0f;
        for (int ph = 0; ph < 2; ++ph) {
            if (w[ph] <= 0.0f) continue;
            Vec3 o0 = normalize(gt.sample(origin[ph], d));
            float cp = 1.0f;
            // Animations (disk view, ~120 km pixels) stop at ANIM_OCTAVES: finer
            // octaves are sub-pixel, and moving them reads as random shimmer.
            for (int n = 0; n < (animating ? ANIM_OCTAVES : GRID_OCTAVES); ++n)
                cp *= cascade_step(n < ADVECTED || animating ? o0 : d, phase_off[ph], n);
            cas += w[ph] * cp;
        }
        tau[c] = gt.sample(cloud, d) / c95 * cas;
        // The finer octaves at the cell centre: only for solving the scale and
        // for the maps; the renderer evaluates them per pixel.
        // Animations skip them: noise fixed to the ground would show as a
        // static pattern the clouds slide through.
        fine[c] = 1.0f;
        if (!animating)
            for (int n = GRID_OCTAVES; n < OCTAVES; ++n) fine[c] *= cascade_step(d, off, n);
    }
    // Bisect the global thickness scale for the target mean opacity.
    auto mean_alpha = [&](float k) {
        double m = 0.0;
        #pragma omp parallel for reduction(+ : m) schedule(static)
        for (int c = 0; c < g.size(); ++c) m += (1.0 - std::exp(-k * tau[c] * fine[c])) * g.area(c);
        return static_cast<float>(m);
    };
    float lo = 0.0f, hi = 64.0f;
    for (int it = 0; it < 40; ++it) {
        float mid = 0.5f * (lo + hi);
        (mean_alpha(mid) < cloud_amount ? lo : hi) = mid;
    }
    P.cloud.resize(g.size());
    P.cloud_tau.resize(g.size());
    P.cloud_noise_off = off;
    P.clouds_animated = animating;
    for (int c = 0; c < g.size(); ++c) {
        P.cloud_tau[c] = lo * tau[c];
        P.cloud[c] = 1.0f - std::exp(-P.cloud_tau[c] * fine[c]);
    }

    // Keep the weather: vorticity for the map, wind for cloud streets at render time.
    P.weather_vorticity.resize(g.size());
    P.cloud_wind.resize(g.size());
    for (int c = 0; c < g.size(); ++c) {
        P.weather_vorticity[c] = gw.sample(zeta, g.dir(c));
        P.cloud_wind[c] = gw.sample(u, g.dir(c));
    }
}

void CloudSimDelete::operator()(CloudSim* s) const { delete s; }

CloudSimPtr start_clouds(Planet& P, const PlanetParams& prm) {
    CloudSimPtr sim(new CloudSim(P, prm));
    // Clouds live ~15 steps, so the tracer forgets its start within ~100: only
    // run it at the end, once the flow is developed; the texture window covers
    // the last TEX_STEPS steps.
    for (int s = 0; s < STEPS; ++s) sim->step(DT, s >= STEPS - 150, s >= STEPS - TEX_STEPS - 1);
    sim->write(P);
    return sim;
}

void advance_clouds(CloudSim& sim, Planet& P, float minutes) {
    if (!sim.animating) {
        // Two fresh phases half a cycle apart, then a pre-roll of one full cycle
        // so frame 0 already shows the steady (large-scale-strained) texture.
        sim.animating = true;
        sim.age[0] = TEX_TIME;
        sim.age[1] = 0.0f;
        for (int c = 0; c < sim.NT; ++c) sim.origin[0][c] = sim.origin[1][c] = sim.gt.dir(c);
        std::uniform_real_distribution<float> U(0.0f, 1.0f);
        sim.phase_off[1] = Vec3(U(sim.rng) * 50.0f, U(sim.rng) * 50.0f, U(sim.rng) * 50.0f);
        for (int s = 0; s < 2 * TEX_STEPS; ++s) sim.step(DT, true, true);
        // Real time: the simulated winds' strength varies a lot between planets
        // (mean |u| ~1.5-6.5 model units), so calibrate on the actual flow —
        // the mean wind moves clouds at ~15 m/s (typical mid-level cloud drift).
        double um = 0.0;
        for (int c = 0; c < sim.NW; ++c) um += length(sim.u[c]) * sim.gw.area(c);
        const float rad_per_min = 15.0f * 60.0f / (EARTH_RADIUS_KM * 1000.0f);
        sim.time_per_minute = rad_per_min / static_cast<float>(std::max(um, 0.1));
    }
    const float t = minutes * sim.time_per_minute;
    const int n = std::max(1, static_cast<int>(std::ceil(t / DT)));
    if (t > 0.0f)
        for (int s = 0; s < n; ++s) sim.step(t / n, true, true);
    sim.write(P);
}

void simulate_clouds(Planet& P, const PlanetParams& prm) { start_clouds(P, prm); }

float cloud_opacity(const Planet& P, Vec3 d, int octaves) {
    if (P.cloud_tau.empty()) return P.grid.sample(P.cloud, d);  // --cloud-image
    float tau = P.grid.sample(P.cloud_tau, d);
    if (tau <= 0.0f) return 0.0f;
    if (P.clouds_animated) octaves = GRID_OCTAVES;  // see CloudSim::write
    for (int n = GRID_OCTAVES; n < octaves; ++n) tau *= cascade_step(d, P.cloud_noise_off, n);
    return 1.0f - std::exp(-tau);
}
