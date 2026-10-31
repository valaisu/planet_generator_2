// Climate: annual-mean temperature, pressure belts, winds, and precipitation from
// moisture evaporated over the oceans and carried along the wind.
//
// Runs on a coarse cube-sphere (~100 km cells) — climate is large-scale — then
// upsamples to the planet grid (temperature re-applies the fine-scale lapse rate).
//   1. Temperature: latitude curve, lapse rate, continentality, and a crude
//      ocean-gyre anomaly (cold currents off subtropical west coasts, warm drift
//      toward subpolar west coasts) found by scanning east/west for land.
//   2. Pressure: zonal annual-mean MSLP profiles read off Earth climatology, one
//      for ocean (ITCZ low, subtropical highs, subpolar lows) and one for land
//      (tropical thermal low, mid-latitude continental highs), blended by a
//      blurred land mask.
//   3. Wind: down the pressure gradient, turned by Coriolis (+ friction).
//   4. Moisture: ocean evaporation (Clausius-Clapeyron), semi-Lagrangian advection
//      along the wind, rain-out from saturation (cooling, incl. lifting over
//      terrain), convergence, and upslope flow -> rain shadows and deserts emerge.
//      Precipitation is normalised so the global mean is Earth's ~1000 mm/yr.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "planet.h"

namespace {
constexpr float PI = 3.14159265358979323846f;
constexpr int CLIMATE_N = 96;

float lat_deg(Vec3 d) { return std::asin(std::clamp(d.y, -1.0f, 1.0f)) * 180.0f / PI; }
float gauss(float x, float mu, float sig) { return std::exp(-0.5f * (x - mu) * (x - mu) / (sig * sig)); }

// Diffusion blur: `iters` passes of 3x3 neighbourhood averaging (sigma grows
// ~ 0.8 cells * sqrt(iters)).
template <class T>
void blur(const CubeGrid& g, std::vector<T>& f, int iters) {
    std::vector<T> tmp(f.size());
    for (int it = 0; it < iters; ++it) {
        #pragma omp parallel for schedule(static)
        for (int c = 0; c < g.size(); ++c) {
            T sum = f[c];
            for (int nb : g.nbr(c)) sum = sum + f[nb];
            tmp[c] = sum * (1.0f / 9.0f);
        }
        f.swap(tmp);
    }
}

// Annual-mean sea-level pressure (hPa) vs |latitude| (deg), every 10 deg.
constexpr float OCEAN_MSLP[10] = {1009, 1012, 1018, 1021, 1015, 1003, 990, 994, 1006, 1013};
constexpr float LAND_MSLP[10] = {1007, 1009, 1012, 1015, 1017, 1018, 1015, 1013, 1012, 1012};
// Catmull-Rom through the table (no flat spots at the nodes — a smoothstep blend
// had zero slope every 10 deg, i.e. dead calm bands that blocked moisture).
// Mirrored at the equator so the profile is smooth across it.
float table(const float* t, float a) {
    float x = std::clamp(a / 10.0f, 0.0f, 9.0f);
    int i = std::min(static_cast<int>(x), 8);
    float f = x - i;
    auto at = [&](int k) { return t[std::clamp(k < 0 ? -k : k, 0, 9)]; };
    float p0 = at(i - 1), p1 = at(i), p2 = at(i + 1), p3 = at(i + 2);
    return 0.5f * (2.0f * p1 + (-p0 + p2) * f + (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3) * f * f
                   + (-p0 + 3.0f * p1 - 3.0f * p2 + p3) * f * f * f);
}

// Saturation moisture relative to 25 C (~7%/K, Clausius-Clapeyron).
float saturation(float t) { return std::exp(0.064f * (t - 25.0f)); }

}  // namespace

void simulate_climate(Planet& P, const PlanetParams& /*prm*/) {
    const CubeGrid& fine = P.grid;
    const CubeGrid g(CLIMATE_N);
    const int N = g.size();

    // --- Coarse terrain: mean elevation (land >= 0) + land fraction per cell.
    std::vector<float> elev(N, 0.0f), land(N, 0.0f), wsum(N, 0.0f);
    for (int c = 0; c < fine.size(); ++c) {
        int k = g.cell_of(fine.dir(c));
        float a = fine.area(c);
        elev[k] += std::max(P.elevation[c], 0.0f) * a;
        land[k] += (P.elevation[c] > 0.0f) * a;
        wsum[k] += a;
    }
    for (int k = 0; k < N; ++k) { elev[k] /= wsum[k]; land[k] /= wsum[k]; }
    std::vector<uint8_t> is_land(N);
    for (int k = 0; k < N; ++k) is_land[k] = land[k] > 0.5f;

    // --- 1. Temperature at sea level ------------------------------------------
    std::vector<float> cont = land;
    blur(g, cont, 60);  // continentality, sigma ~1000 km

    // Ocean-gyre anomaly: scan along the parallel for the nearest land east/west.
    std::vector<float> anom(N, 0.0f);
    const float step = 100.0f / EARTH_RADIUS_KM;
    #pragma omp parallel for schedule(dynamic, 64)
    for (int c = 0; c < N; ++c) {
        if (is_land[c]) continue;
        Vec3 d = g.dir(c);
        float a = std::fabs(lat_deg(d));
        float coslat = std::max(0.2f, std::sqrt(1.0f - d.y * d.y));
        auto scan = [&](float sign) {
            for (int s = 1; s <= 30; ++s) {  // up to 3000 km
                Vec3 q = rotate_about(d, Vec3(0, 1, 0), sign * s * step / coslat);
                if (is_land[g.cell_of(q)]) return std::exp(-(s * 100.0f) / 900.0f);
            }
            return 0.0f;
        };
        float east = scan(1.0f), west = scan(-1.0f);
        float sub = gauss(a, 25.0f, 12.0f), pol = gauss(a, 55.0f, 10.0f);
        anom[c] = sub * (-6.0f * east + 2.5f * west) + pol * (6.0f * east - 4.0f * west);
    }
    blur(g, anom, 12);

    std::vector<float> t_sea(N), t_air(N);
    for (int c = 0; c < N; ++c) {
        float a = std::fabs(lat_deg(g.dir(c)));
        float t = 27.0f - 47.0f * std::pow(a / 90.0f, 1.5f);
        t += cont[c] * (3.0f - 25.0f * std::pow(a / 90.0f, 1.5f));
        t += anom[c];
        t_sea[c] = t;  // over the sea: the surface-air temperature (sea ice below ~-5)
        t_air[c] = t_sea[c] - 6.5f * elev[c] / 1000.0f;
    }

    // --- 2. Pressure belts ------------------------------------------------------
    std::vector<float> lblur = land;
    blur(g, lblur, 90);  // sigma ~12 deg, softens the land/sea contrast
    std::vector<float> pres(N);
    for (int c = 0; c < N; ++c) {
        float lat = lat_deg(g.dir(c));
        float a = std::fabs(lat);
        float pl = table(LAND_MSLP, a), po = table(OCEAN_MSLP, a);
        pres[c] = po + (pl - po) * lblur[c];
    }

    // --- 3. Wind -----------------------------------------------------------------
    std::vector<Vec3> wind(N);
    for (int c = 0; c < N; ++c) {
        Vec3 d = g.dir(c);
        Vec3 gp = g.gradient(pres, c);
        // Friction leaves surface winds ~25-30 deg across the isobars.
        wind[c] = gp * -1.0f + cross(d, gp) * (8.0f * d.y);
    }
    // Coriolis vanishes at the equator, which leaves the deep tropics with purely
    // meridional flow; real trades stay easterly right up to the ITCZ (they carry
    // Atlantic moisture deep into the Amazon/Congo). Add that easterly.
    {
        std::vector<float> mag(N);
        for (int c = 0; c < N; ++c) mag[c] = length(wind[c]);
        std::nth_element(mag.begin(), mag.begin() + N / 2, mag.end());
        const float typical = mag[N / 2];
        for (int c = 0; c < N; ++c) {
            Vec3 e, n;
            CubeGrid::tangent_basis(g.dir(c), e, n);
            wind[c] = wind[c] - e * (1.2f * typical * gauss(lat_deg(g.dir(c)), 0.0f, 12.0f));
        }
    }
    blur(g, wind, 4);
    for (int c = 0; c < N; ++c) {  // re-project onto the tangent plane
        Vec3 d = g.dir(c);
        wind[c] = wind[c] - d * dot(wind[c], d);
    }
    // Scale so a fast wind moves ~0.8 cell per advection step.
    std::vector<float> speed(N);
    for (int c = 0; c < N; ++c) speed[c] = length(wind[c]);
    std::vector<float> sorted = speed;
    std::nth_element(sorted.begin(), sorted.begin() + N * 95 / 100, sorted.end());
    const float wscale = 0.8f * g.spacing() / std::max(1e-6f, sorted[N * 95 / 100]);
    for (Vec3& w : wind) w = w * wscale;

    std::vector<float> div(N), upslope(N);
    for (int c = 0; c < N; ++c) {
        div[c] = g.divergence(wind, c) / wscale;
        // Rise rate of air pushed up terrain: wind . grad(h), in km per cell step.
        upslope[c] = std::max(0.0f, dot(wind[c], g.gradient(elev, c))) / 1000.0f;
    }
    std::vector<float> hadley(N);
    for (int c = 0; c < N; ++c)
        hadley[c] = gauss(std::fabs(lat_deg(g.dir(c))), 24.0f, 8.0f)
                    * std::clamp(1.0f - 0.25f * anom[c], 0.3f, 1.5f);
    // Normalise convergence to a robust scale.
    std::vector<float> absdiv(N);
    for (int c = 0; c < N; ++c) absdiv[c] = std::fabs(div[c]);
    std::nth_element(absdiv.begin(), absdiv.begin() + N * 9 / 10, absdiv.end());
    const float divn = std::max(1e-6f, absdiv[N * 9 / 10]);

    // --- 4. Moisture transport + rain-out -----------------------------------------
    std::vector<float> q(N, 0.0f), qn(N), rain(N, 0.0f), rain_acc(N, 0.0f), cap(N);
    constexpr int STEPS = 700, AVG_FROM = 350;
    // Rain-out coefficients: base, convergence, upslope, warm convection, recycling.
    // Then saturation-excess rain fraction and Hadley subsidence strength. Tuned
    // on real Earth relief (see CLAUDE.md); CLIMATE_K="k0,...,k6" overrides them.
    // Rates are low on purpose: vapour should travel thousands of km inland.
    float K[7] = {0.003f, 0.012f, 0.3f, 0.012f, 0.7f, 0.5f, 3.0f};
    if (const char* e = std::getenv("CLIMATE_K"))
        std::sscanf(e, "%f,%f,%f,%f,%f,%f,%f", &K[0], &K[1], &K[2], &K[3], &K[4], &K[5], &K[6]);
    for (int c = 0; c < N; ++c) cap[c] = saturation(t_air[c]);
    for (int s = 0; s < STEPS; ++s) {
        // Sources: ocean evaporation toward saturation (less over cold currents);
        // land recycles part of last step's rain (evapotranspiration).
        #pragma omp parallel for schedule(static)
        for (int c = 0; c < N; ++c) {
            float e = is_land[c] ? K[4] * rain[c]
                                 : 0.1f * std::max(0.0f, 0.9f * saturation(std::max(t_sea[c], -2.0f)) - q[c]);
            q[c] += e;
        }
        // Semi-Lagrangian advection: pull moisture from upwind.
        #pragma omp parallel for schedule(static)
        for (int c = 0; c < N; ++c) {
            Vec3 src = normalize(g.dir(c) - wind[c]);
            qn[c] = g.sample(q, src);
        }
        q.swap(qn);
        // Rain-out: a fraction of the column per step — more where air converges,
        // is lifted up terrain or is warm (convection), less where it sinks
        // (subtropical highs) or sits over cold water — plus any saturation excess.
        #pragma omp parallel for schedule(static)
        for (int c = 0; c < N; ++c) {
            float dv = div[c] / divn;
            float rate = K[0] + K[1] * std::clamp(-dv, 0.0f, 2.0f)
                         + K[2] * std::clamp(upslope[c], 0.0f, 1.0f)
                         + K[3] * std::clamp((t_air[c] - 18.0f) / 10.0f, 0.0f, 1.0f);
            rate *= std::exp(-0.8f * std::max(0.0f, dv));
            // Hadley-cell descent dries the subtropics from aloft (the Sahara even
            // has a surface heat low) — except where warm western-boundary water
            // destabilises the air (subtropical east coasts stay wet).
            rate *= std::exp(-K[6] * hadley[c]);
            if (anom[c] < 0.0f) rate *= std::exp(0.12f * anom[c]);
            float r = q[c] * std::min(rate, 0.9f) + std::max(0.0f, q[c] - cap[c]) * K[5];
            r = std::min(r, q[c]);
            q[c] -= r;
            rain[c] = r;
            if (s >= AVG_FROM) rain_acc[c] += r;
        }
    }
    if (std::getenv("CLIMATE_DEBUG")) {
        double rl = 0, ro = 0, ql = 0, qo = 0, al = 0, ao = 0, cl = 0, co = 0;
        for (int c = 0; c < N; ++c) {
            double a = g.area(c);
            if (is_land[c]) { rl += rain_acc[c] * a; ql += q[c] * a; cl += cap[c] * a; al += a; }
            else { ro += rain_acc[c] * a; qo += q[c] * a; co += cap[c] * a; ao += a; }
        }
        double sl = 0, so = 0;
        for (int c = 0; c < N; ++c) (is_land[c] ? sl : so) += length(wind[c]) / g.spacing() * g.area(c);
        std::fprintf(stderr, "wind cells/step land %.3f ocean %.3f\n", sl / al, so / ao);
        std::fprintf(stderr, "rain land %.4g ocean %.4g | q land %.4g ocean %.4g | cap land %.4g ocean %.4g\n",
                     rl / al, ro / ao, ql / al, qo / ao, cl / al, co / ao);
    }
    // Normalise: area-weighted global mean -> 1000 mm/yr.
    double mean = 0.0;
    for (int c = 0; c < N; ++c) mean += rain_acc[c] * g.area(c);
    for (float& r : rain_acc) r = static_cast<float>(r * 1000.0 / std::max(1e-12, mean));
    std::vector<float> rh(N);
    for (int c = 0; c < N; ++c) rh[c] = std::clamp(q[c] / cap[c], 0.0f, 1.0f);
    blur(g, rain_acc, 1);

    // --- Upsample to the planet grid ----------------------------------------------
    const int F = fine.size();
    P.temperature.resize(F);
    P.pressure.resize(F);
    P.wind.resize(F);
    P.precip.resize(F);
    P.humidity.resize(F);
    P.sst_anomaly.resize(F);
    #pragma omp parallel for schedule(static)
    for (int c = 0; c < F; ++c) {
        Vec3 d = fine.dir(c);
        P.temperature[c] = g.sample(t_sea, d) - 6.5f * std::max(P.elevation[c], 0.0f) / 1000.0f;
        P.pressure[c] = g.sample(pres, d);
        P.wind[c] = g.sample(wind, d) / wscale;
        P.precip[c] = g.sample(rain_acc, d);
        P.humidity[c] = g.sample(rh, d);
        P.sst_anomaly[c] = g.sample(anom, d);
    }
}
