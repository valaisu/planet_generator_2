// Hydraulic erosion: implicit stream-power law (Braun & Willett 2013) on land.
//
// Each iteration:
//   1. Priority-flood from the sea (Barnes et al. 2014) gives every land cell a
//      receiver it drains to — routing straight through closed basins, so every
//      river reaches the ocean and the processing order is downstream-first.
//   2. Drainage area accumulates upstream -> downstream.
//   3. dh/dt = -K A^m S, solved implicitly in downstream-first order:
//      h = (h + F h_recv) / (1 + F), F = K dt A^m / L. Unconditionally stable.
// A few dozen iterations carve dendritic valleys into the tectonic relief while
// keeping its large-scale shape (no uplift term: this is wear, not equilibrium).
#include <algorithm>
#include <cmath>
#include <queue>

#include "planet.h"

namespace {
// Steps 1-2 on the current elevation: receivers (priority flood from the sea)
// and drainage area (km^2); `order` lists land cells downstream-first.
void route_flow(const CubeGrid& g, const std::vector<float>& h, std::vector<int>& recv,
                std::vector<float>& area, std::vector<int>& order) {
    const int N = g.size();
    const float KM = EARTH_RADIUS_KM;
    const float sphere_km2 = 4.0f * 3.14159265f * KM * KM;
    using Item = std::pair<float, int>;
    std::priority_queue<Item, std::vector<Item>, std::greater<Item>> pq;
    std::vector<uint8_t> done(N, 0);
    recv.assign(N, -1);
    area.assign(N, 0.0f);
    order.clear();
    for (int c = 0; c < N; ++c) {
        if (h[c] > 0.0f) continue;
        done[c] = 1;  // ocean = base level
        for (int nb : g.nbr(c))
            if (h[nb] > 0.0f && !done[nb] && (recv[nb] < 0 || h[c] < h[recv[nb]])) recv[nb] = c;
    }
    for (int c = 0; c < N; ++c)
        if (recv[c] >= 0) pq.push({h[c], c});
    while (!pq.empty()) {
        auto [hc, c] = pq.top();
        pq.pop();
        if (done[c]) continue;
        done[c] = 1;
        order.push_back(c);
        for (int nb : g.nbr(c)) {
            if (done[nb] || recv[nb] >= 0) continue;
            // Lowest-first expansion: the first cell to reach a neighbour is
            // where it drains (through any pit, at the spill level).
            recv[nb] = c;
            pq.push({std::max(h[nb], hc), nb});
        }
    }
    for (int c : order) area[c] = g.area(c) * sphere_km2;
    for (auto i = order.rbegin(); i != order.rend(); ++i)
        if (h[recv[*i]] > 0.0f) area[recv[*i]] += area[*i];
}
}  // namespace

void compute_drainage(Planet& P) {
    const CubeGrid& g = P.grid;
    std::vector<int> order;
    route_flow(g, P.elevation, P.flow_to, P.drainage, order);
    // Re-accumulate weighted by runoff = rain - evaporation (Turc-Pike, with
    // potential evaporation from temperature), so desert rivers are small unless
    // fed from wet uplands (the Nile). Units: km^2 at 1000 mm/yr of runoff.
    const float sphere_km2 = 4.0f * 3.14159265f * EARTH_RADIUS_KM * EARTH_RADIUS_KM;
    for (int c : order) {
        float p = P.precip[c], e0 = std::max(100.0f, 300.0f + 50.0f * P.temperature[c]);
        float runoff = p - p * e0 / std::sqrt(p * p + e0 * e0);
        P.drainage[c] = g.area(c) * sphere_km2 * runoff / 1000.0f;
    }
    for (auto i = order.rbegin(); i != order.rend(); ++i)
        if (P.elevation[P.flow_to[*i]] > 0.0f) P.drainage[P.flow_to[*i]] += P.drainage[*i];
}

void erode(Planet& P, const PlanetParams& /*prm*/) {
    const CubeGrid& g = P.grid;
    const int N = g.size();
    const float KM = EARTH_RADIUS_KM;
    constexpr int ITERS = 40;
    constexpr float K = 0.02f, M = 0.5f;

    std::vector<float>& h = P.elevation;
    std::vector<int> recv(N), order;
    std::vector<float> area(N);
    order.reserve(N);
    for (int it = 0; it < ITERS; ++it) {
        // 1-2. Receivers + drainage area.
        route_flow(g, h, recv, area, order);

        // 3. Implicit stream-power update, downstream first.
        for (int c : order) {
            int r = recv[c];
            if (h[c] <= h[r]) continue;  // inside a closed basin: leave the lake bed
            float L = length(g.dir(c) - g.dir(r)) * KM;
            float F = K * std::pow(area[c], M) / L;
            h[c] = (h[c] + F * h[r]) / (1.0f + F);
            h[c] = std::max(h[c], 1.0f);  // erosion alone never drowns land
        }
    }
}
