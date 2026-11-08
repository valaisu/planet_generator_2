// derive(): physical inputs -> planet physics, in dependency order. Every model
// here is deliberately simple; the constants that were calibrated to a real body
// say so. Bodies that defy a model (Venus' missing N2, Mercury's 3:2 resonance...)
// are left wrong and listed in check_presets(), not patched with special cases.
#include "physics.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <random>

namespace {

constexpr double PI = 3.14159265358979323846;
constexpr double G = 6.674e-11, SIGMA = 5.670e-8, R_GAS = 8.314;
constexpr double M_SUN = 1.989e30, M_EARTH = 5.972e24, R_EARTH = 6.371e6, AU = 1.496e11;
constexpr double GYR = 3.156e16, S_EARTH = 1361.0;

double clamp01(double x) { return std::clamp(x, 0.0, 1.0); }
// Molten rock radiates at ~1500 K; the surface brightness temperature at which lava
// shows (crust cracks) is CRUST_K (planet.h, shared with the renderer).
constexpr double T_LAVA = LAVA_K;
// CO2 in the air of a snowball: the ocean froze with its CO2 in carbonate rock;
// volcanoes add it back over ~10 Myr until the ice melts (~0.1 bar for Earth).
constexpr double SNOWBALL_CO2 = 2e-3;

// --- Phase diagrams -----------------------------------------------------------
// Clausius-Clapeyron from the triple point with constant latent heats (sublimation
// below it, vaporisation above); fits the 1-bar boiling/sublimation points.
struct Species {
    const char* name;
    double t_triple, p_triple;   // K, bar
    double l_sub, l_vap;         // J/mol
    double t_crit;               // K
    double mu;                   // g/mol
    double rayleigh;             // Rayleigh cross-section relative to air
};
const Species H2O{"H2O", 273.16, 0.006117, 51.06e3, 43.5e3, 647.1, 18, 0.75};
const Species CO2{"CO2", 216.58, 5.185, 26.2e3, 16.6e3, 304.1, 44, 2.45};
const Species N2{"N2", 63.15, 0.1252, 6.7e3, 5.98e3, 126.2, 28, 1.0};
const Species CH4{"CH4", 90.69, 0.1170, 9.6e3, 8.66e3, 190.6, 16, 2.3};
const double MU_O2 = 32, RAYLEIGH_O2 = 0.86;

double p_sat(const Species& s, double t) {  // bar; infinite above the critical point
    if (t >= s.t_crit) return 1e30;
    double l = t < s.t_triple ? s.l_sub : s.l_vap;
    return s.p_triple * std::exp(-l / R_GAS * (1.0 / t - 1.0 / s.t_triple));
}

// --- Star -----------------------------------------------------------------------
double main_sequence_lum(double m) {  // solar units, at the Sun's present stage
    return m < 0.43 ? 0.23 * std::pow(m, 2.3) : m < 2.0 ? std::pow(m, 4.0) : 1.4 * std::pow(m, 3.5);
}

// Relative Planck radiance at R/G/B (610/550/465 nm), normalised to the brightest.
void blackbody_rgb(double teff, double rgb[3]) {
    const double lam[3] = {610e-9, 550e-9, 465e-9};
    double mx = 0;
    for (int i = 0; i < 3; ++i) {
        rgb[i] = 1.0 / (std::pow(lam[i], 5) * (std::exp(1.4388e-2 / (lam[i] * teff)) - 1.0));
        mx = std::max(mx, rgb[i]);
    }
    for (int i = 0; i < 3; ++i) rgb[i] /= mx;
}

// XUV fluence received over `age` (units: saturated luminosity x Gyr). Saturated
// until t_sat, then ~t^-1.23 (Ribas et al. 2005); M dwarfs stay saturated longer.
double xuv_fluence(double age, double t_sat) {
    if (age <= t_sat) return age;
    return t_sat * (1.0 + (1.0 - std::pow(age / t_sat, -0.23)) / 0.23);
}

// --- Insolation geometry --------------------------------------------------------
// Annual-mean daily insolation at a latitude, relative to the global mean S/4.
double annual_insolation(double lat, double obliquity) {
    const int steps = 360;
    double sum = 0;
    for (int k = 0; k < steps; ++k) {
        double dec = std::asin(std::sin(obliquity) * std::sin(2 * PI * (k + 0.5) / steps));
        double c = std::clamp(-std::tan(lat) * std::tan(dec), -1.0, 1.0);
        double h0 = std::acos(c);
        sum += (h0 * std::sin(lat) * std::sin(dec) + std::cos(lat) * std::cos(dec) * std::sin(h0)) / PI;
    }
    return 4.0 * sum / steps;
}

// Radiogenic heating per kg of silicate (Earth's mantle: 5e-12 W/kg today),
// shares of U238 / U235 / Th232 / K40 today and their half-lives.
double radiogenic_w_per_kg(double age_gyr) {
    const double share[4] = {0.42, 0.02, 0.42, 0.14}, half[4] = {4.468, 0.704, 14.0, 1.25};
    double h = 0;
    for (int i = 0; i < 4; ++i) h += share[i] * std::pow(2.0, (4.5 - age_gyr) / half[i]);
    return 5e-12 * h;
}

// Neukum crater chronology: craters > 1 km per km^2 on a surface of age t (Gyr).
double neukum(double t) { return 5.44e-14 * (std::exp(6.93 * t) - 1.0) + 8.38e-4 * t; }

const char* regime_name(Regime r) {
    switch (r) {
        case Regime::Auto: return "auto";
        case Regime::PlateTectonics: return "plate tectonics";
        case Regime::StagnantLid: return "stagnant lid";
        case Regime::Dead: return "stagnant lid, geologically dead";
        case Regime::HeatPipe: return "heat-pipe (Io-like)";
        case Regime::IceShellActive: return "ice shell, convecting";
        case Regime::IceShellInactive: return "ice shell, inactive";
    }
    return "?";
}

// Photosynthetic pigment by star temperature (Kiang et al. 2007, speculative):
// M dwarfs -> near-black/purple, K -> red-brown, Sun -> green, F -> orange.
void pigment(double teff, double rgb[3]) {
    const double t[4] = {3000, 4500, 5800, 7000};
    const double c[4][3] = {{0.10, 0.07, 0.12}, {0.35, 0.22, 0.10}, {0.20, 0.35, 0.12}, {0.55, 0.35, 0.10}};
    double x = std::clamp(teff, t[0], t[3]);
    int i = x < t[1] ? 0 : x < t[2] ? 1 : 2;
    double f = (x - t[i]) / (t[i + 1] - t[i]);
    for (int k = 0; k < 3; ++k) rgb[k] = c[i][k] * (1 - f) + c[i + 1][k] * f;
}

}  // namespace

PlanetPhysics derive(const PhysicalInputs& in) {
    PlanetPhysics p;
    const double age = in.age_gyr;
    const bool icy = in.composition == Composition::IceRock;

    // --- Star -> insolation ----------------------------------------------------
    p.star_lifetime_gyr = 10.0 * std::pow(in.star_mass, -2.5);
    auto lum_at = [&](double t_gyr) {  // brightens ~40% over the main sequence
        return main_sequence_lum(in.star_mass) / (1.0 + 0.4 * (1.0 - std::min(t_gyr / p.star_lifetime_gyr, 1.0) / 0.457));
    };
    p.star_lum = lum_at(age);
    double r_star = std::pow(in.star_mass, in.star_mass < 1.0 ? 0.8 : 0.57);
    p.star_teff = 5772.0 * std::pow(p.star_lum / (r_star * r_star), 0.25);
    blackbody_rgb(p.star_teff, p.star_rgb);
    p.insolation = p.star_lum / (in.orbit_au * in.orbit_au);
    double t_sat = std::clamp(0.1 * std::pow(in.star_mass, -3.3), 0.05, 3.0);
    p.xuv_rel = p.insolation * xuv_fluence(age, t_sat) / xuv_fluence(4.57, 0.1);

    // --- Mass + composition -> radius, gravity --------------------------------
    // Uncompressed density of the iron / rock / ice mix, then self-compression
    // ~(1 + 0.35 M^0.6) (Earth 1.35; fits 5 M_E super-Earths ~1.5 R_E).
    p.core_fraction = in.core_fraction >= 0 ? in.core_fraction
                    : in.composition == Composition::IronRich ? 0.68 : icy ? 0.15 : 0.32;
    const double w = in.volatile_fraction;
    double f_core = p.core_fraction * (1 - w), f_rock = (1 - p.core_fraction) * (1 - w);
    double rho0 = 1.0 / (f_core / 7900.0 + f_rock / 3300.0 + w / 1000.0);
    p.density = rho0 * (1.0 + 0.35 * std::pow(in.mass, 0.6));
    const double m_kg = in.mass * M_EARTH;
    const double radius = std::cbrt(3.0 * m_kg / (4.0 * PI * p.density));
    const double area = 4.0 * PI * radius * radius;
    p.radius_km = radius / 1e3;
    p.gravity = G * m_kg / (radius * radius);
    p.v_esc_kms = std::sqrt(2.0 * G * m_kg / radius) / 1e3;
    auto column_bar = [&](double kg) { return kg * p.gravity / area / 1e5; };

    // --- Rotation and tides ----------------------------------------------------
    // Despinning time from a primordial ~10 h day (Gladman et al. 1996),
    // I = 0.4 m R^2, Q/k2 = 330.
    const bool moon = in.host_mass_kg > 0;
    double perturber = moon ? in.host_mass_kg : in.star_mass * M_SUN;
    double a = moon ? in.host_distance_km * 1e3 : in.orbit_au * AU;
    double omega0 = 2 * PI / (10.0 * 3600.0);
    double t_lock = omega0 * std::pow(a, 6) * 0.4 * m_kg * 330.0 / (3.0 * G * perturber * perturber * std::pow(radius, 3));
    p.lock_time_gyr = t_lock / GYR;
    p.tidally_locked = p.lock_time_gyr < age;
    double orbit_s = 2 * PI * std::sqrt(a * a * a / (G * (perturber + m_kg)));
    p.rotation_hours = p.tidally_locked ? orbit_s / 3600.0 : in.day_hours;
    if (moon) {  // E = 21/2 (k2/Q) G M^2 R^5 n e^2 / a^6 (Peale et al. 1979);
                 // k2/Q: Io measured 0.015, icy moon with an ocean ~0.25/100
        double n = 2 * PI / orbit_s, k2q = icy ? 0.0025 : 0.015;
        double e = in.host_eccentricity;
        p.tidal_heat_wm2 = 10.5 * k2q * G * perturber * perturber * std::pow(radius, 5) * n * e * e / std::pow(a, 6) / area;
    }
    const bool locked_to_star = p.tidally_locked && !moon;

    // --- Interior heat ----------------------------------------------------------
    // Radiogenic heat of the silicate, plus secular cooling (~equal share for an
    // Earth-sized body, less for small ones), plus tides.
    double radiogenic = radiogenic_w_per_kg(age) * f_rock * m_kg;
    p.heat_flux_wm2 = radiogenic * (1.0 + std::min(radius / R_EARTH, 1.5)) / area + p.tidal_heat_wm2;
    const double q = p.heat_flux_wm2;

    // --- Formation / impact heat: a magma ocean ---------------------------------
    // Accretion and giant impacts melt the surface. The magma ocean radiates until
    // a crust closes over it: ~1 Myr dry, a few Myr with Earth's water, ~30x longer
    // under a steam atmosphere that can't cool inside the runaway limit (Elkins-
    // Tanton 2008; Hamano et al. 2013). The molten share falls ~linearly; its
    // radiation enters the climate (icy bodies: no silicate melt at the surface).
    p.melt_myr = age * 1000.0;
    if (in.impact_myr_ago >= 0) p.melt_myr = std::min(p.melt_myr, in.impact_myr_ago);
    const double water_rel = std::min(1.0, w / 2.3e-4);
    p.magma_tau_myr = icy ? 0.0 : (1.0 + 3.0 * water_rel) * (water_rel > 0.1 && p.insolation > 1.1 ? 30.0 : 1.0);
    p.melt = p.magma_tau_myr > 0 ? clamp01(1.0 - p.melt_myr / p.magma_tau_myr) : 0.0;
    const double q_magma = p.melt * SIGMA * std::pow(T_LAVA, 4);
    // Once the crust has closed it thickens ~sqrt(t) and the melt beneath keeps
    // breaking out along its cracks for a few closing times (fading; a guess at
    // ~5% of the surface at first). Its heat no longer matters for the climate.
    if (p.magma_tau_myr > 0 && p.melt <= 0)
        p.rift = 0.05 * clamp01(1.0 - (p.melt_myr - p.magma_tau_myr) / (3.0 * p.magma_tau_myr));

    // --- Volatile inventories (as surface pressure if all were gas) -------------
    // Rocky: per kg of water, 0.33 kg CO2 (Earth's carbonates ~ Venus' air) and
    // 0.003 kg N2 (Earth's air). Icy: N2 1.5e-4 (Titan's air, from NH3) and CH4
    // 2e-5 per kg of water ice, but only for bodies formed where the disk was
    // colder than ~100 K (T ~ 280 K L^1/4 / sqrt(a)): Titan and Pluto, not the
    // Galilean moons.
    double water_kg = w * m_kg;
    double t_disk = 280.0 * std::pow(main_sequence_lum(in.star_mass), 0.25) / std::sqrt(in.orbit_au);
    bool cold_ices = icy && t_disk < 100.0;
    double inv_co2 = icy ? 0 : column_bar(0.33 * water_kg);
    double inv_n2 = icy ? (cold_ices ? column_bar(1.5e-4 * water_kg) : 0.0) : column_bar(0.003 * water_kg);
    double inv_ch4 = cold_ices ? column_bar(2e-5 * water_kg) : 0;

    // --- Escape: the cosmic shoreline (Zahnle & Catling 2017) --------------------
    // XUV fluence / v_esc^4 relative to Earth. Bodies above ~4 lose their air
    // (Titan 3.7 keeps it, Ganymede ~10 doesn't); slope 8.3 per decade is fitted
    // to Mars keeping 6 mbar of ~14 bar of CO2. Water (rocky bodies) is lost
    // more gently through H escape (slope 2.5).
    p.escape_ratio = p.xuv_rel / std::pow(p.v_esc_kms / 11.19, 4);
    double over = std::max(0.0, std::log10(p.escape_ratio / 4.0));
    double keep_gas = std::pow(10.0, -8.3 * over);
    if (!icy) water_kg *= std::pow(10.0, -2.5 * over);

    // --- Insolation geometry ----------------------------------------------------
    const double obl = in.obliquity_deg * PI / 180.0;
    double s_eq = annual_insolation(0.0, obl), s_pole = annual_insolation(PI / 2 - 1e-6, obl);
    const double d_rad_spin = std::pow(s_eq, 0.25) - std::pow(s_pole, 0.25);  // radiative eq-pole contrast / T_mean
    const double d_rad_locked = std::pow(4.0, 0.25);                          // day (substellar) vs night side
    double d_rad = locked_to_star ? d_rad_locked : d_rad_spin;
    double insol = p.insolation;  // the climate runs below also look at the young system
    const double omega = 2 * PI / (p.rotation_hours * 3600.0);

    // --- Climate iteration --------------------------------------------------------
    // Surface T from a grey atmosphere, T^4 = T_eq^4 (1 + 0.75 tau) + q/sigma, with
    // tau from CO2 (fitted: Earth 0.2 at 400 ppm, Venus 116 at 71 bar), water
    // vapour (Earth's rest at 288 K; runs away when hot) and N2/CH4 collision-
    // induced absorption (Titan); absorber columns scale with p/g. Albedo, ice, phases, CO2 weathering and the
    // runaway greenhouse all depend on T, so iterate.
    struct State {
        double t, p_co2, p_n2, p_ch4, p_h2o, p_o2, pressure;
        double albedo, eta, d_t, ocean_frac, ice_frac;
        bool ocean, frozen, weathering, life, deck, haze, dust;
        double co2_cond, n2_cond, ch4_cond;   // condensed parts (bar)
        double tau;
    };
    double t = 0;
    bool runaway = false;
    double water_lost = 1.0;
    State s{};
    double p_last = 0, tau_last = 0;  // previous iteration's pressure and opacity (set the redistribution)
    double hadley_rad = 0;
    double q_extra = 0;          // magma ocean radiation (the epoch being run)
    bool snowball_now = false;   // this run starts frozen and keeps the CO2 in rock

    // Everything at a given mean temperature (and CO2 pressure if weathering sets
    // it); returns the new surface temperature.
    auto evaluate = [&](double t_mean, double co2_override, State& st) {
        st = State{};
        st.t = t_mean;
        // Redistribution: radiative time of the column (x (1 + tau): an opaque
        // atmosphere cools by diffusion) vs advection over R at ~15 m/s (x27
        // fitted to Earth's ~46 K equator-pole contrast).
        double tau_rad = p_last * 1e5 * 1000.0 / (4.0 * p.gravity * SIGMA * std::pow(t_mean, 3)) * (1.0 + tau_last);
        st.eta = tau_rad / (tau_rad + 27.0 * radius / 15.0);
        st.d_t = t_mean * d_rad * (1.0 - st.eta) * std::clamp(1.0 - q_extra / (SIGMA * std::pow(t_mean, 4)), 0.0, 1.0);  // sunlit share
        double t_eqr = t_mean + st.d_t / 3.0, t_pol = t_mean - 2.0 * st.d_t / 3.0;
        double t_cold = std::max(std::min(t_eqr, t_pol), 20.0);

        // Cold-trapped species: the atmosphere holds at most the vapour pressure
        // at the coldest place; the rest is condensed there. Escape drains a
        // condensed reservoir through its vapour: over Gyr everything goes unless
        // the vapour is < ~1e-3 of it (Pluto's N2 at 37 K).
        auto trap = [&](const Species& sp, double inv, double& cond) {
            if (inv <= 0) { cond = 0; return 0.0; }
            double vapour = std::min(inv, p_sat(sp, t_cold));
            double kept = inv * (1.0 - (1.0 - keep_gas) * std::min(1.0, 1000.0 * vapour / inv));
            double gas = std::min(kept, p_sat(sp, t_cold));
            cond = kept - gas;
            return gas;
        };
        st.p_n2 = trap(N2, inv_n2, st.n2_cond);
        st.p_ch4 = trap(CH4, inv_ch4, st.ch4_cond);
        double co2_gas = trap(CO2, inv_co2, st.co2_cond);

        // Water (rocky bodies: a surface reservoir; icy: the bulk crust).
        double gel = icy ? 0.0 : water_kg * water_lost / (1000.0 * area);
        double p_guess = st.p_n2 + co2_gas + st.p_ch4;
        st.ocean = gel > 1.0 && t_eqr > 271.0 && p_guess > H2O.p_triple && p_sat(H2O, t_mean) < std::max(p_guess, 0.01);
        st.frozen = snowball_now && gel > 1.0 && !st.ocean && t_eqr <= 271.0;  // ocean frozen over
        double h0 = 1100.0 * std::sqrt(9.81 / p.gravity);  // hypsometry: Earth's 2.7 km -> 71% ocean
        st.ocean_frac = st.ocean || st.frozen ? gel / (gel + h0) : 0.0;
        double h2o_inv = icy ? 0.0 : column_bar(water_kg * water_lost);
        st.p_h2o = std::min(0.6 * p_sat(H2O, t_mean), h2o_inv);

        // Ice cover: the area colder than 263 K (P2 profile), limited by supply
        // (3 km thick caps) when there is no ocean.
        double cold_area;
        if (st.d_t > 1e-6) {
            double s2 = (t_mean + st.d_t / 3.0 - 263.0) / st.d_t;  // sin^2(lat) of the ice edge
            cold_area = s2 <= 0 ? 1.0 : s2 >= 1 ? 0.0 : 1.0 - std::sqrt(s2);
        } else cold_area = t_mean < 263.0 ? 1.0 : 0.0;
        st.ice_frac = icy ? 1.0 : st.ocean || st.frozen ? cold_area : std::min(cold_area, gel / 3000.0);

        // Carbonate-silicate thermostat: needs liquid water on a geologically
        // active body; CO2 is then whatever keeps ~288 K (Earth's set point).
        bool can_plate = in.regime == Regime::Auto || in.regime == Regime::PlateTectonics;
        st.weathering = st.ocean && can_plate && in.mass > 0.3 && q > 0.04;
        st.p_co2 = st.weathering ? (co2_override > 0 ? co2_override : co2_gas)
                 : st.frozen ? std::min(co2_gas, SNOWBALL_CO2) : co2_gas;

        st.life = in.life == Tristate::Yes ||
                  (in.life == Tristate::Auto && st.ocean && t_eqr > 273.0 && t_eqr < 330.0 && age > 0.8);
        st.p_o2 = st.life && age > 2.4 ? 0.27 * st.p_n2 : 0.0;
        st.pressure = st.p_n2 + st.p_co2 + st.p_ch4 + st.p_h2o + st.p_o2;

        // Aerosols that set the albedo.
        double volc = std::max(0.0, q - 0.015) / 0.065;
        st.deck = !st.ocean && !st.frozen && st.pressure > 10.0 && volc > 0.2 && st.p_h2o > 1e-5;  // H2SO4 (Venus)
        st.haze = st.p_ch4 > 1e-3 && st.p_n2 > 0.1;                                  // thick tholin haze (Titan)
        st.dust = !icy && !st.ocean && st.pressure > 1e-3 && st.pressure < 0.1        // dust (Mars), needs
                  && st.ice_frac < 0.9;                                                // bare ground to lift

        // Albedo by surface material (+ clouds), measured values of each kind.
        double a_ice = 0.68;
        if (st.n2_cond > 0 || st.ch4_cond > 0) a_ice = 0.7;            // fresh volatile ices
        if (inv_ch4 > 0) a_ice *= 0.8;                                   // tholin-darkened
        bool airless = st.pressure < 1e-6;
        double a_land = airless ? 0.09                                   // space-weathered regolith
                      : st.life ? 0.17                                   // vegetation + deserts
                      : (w > 1e-6 && t_mean < 500) ? 0.25                // iron-oxide dust
                      : 0.12;                                            // basalt
        if (icy) a_land = a_ice;
        double a_surf = (1 - st.ice_frac) * (st.ocean_frac * 0.06 + (1 - st.ocean_frac) * a_land) + st.ice_frac * a_ice;
        if (st.deck) st.albedo = 0.76;
        else if (st.haze) st.albedo = 0.27;
        else st.albedo = 1.0 - (1.0 - a_surf) * (st.ocean ? 0.8 : 1.0);  // water clouds

        // absorber column ~ p/g, pressure-broadened by the total pressure
        double col = 9.81 / p.gravity;
        double tau_co2 = 4.18 * std::pow(std::max(st.p_co2 * col * st.pressure, 0.0), 0.389);
        double tau_h2o = 5.79 * std::sqrt(std::max(st.p_h2o * col * st.pressure, 0.0));
        // N2/CH4 CIA lies in the far-IR where only cold bodies emit (Titan-fitted)
        double tau_cia = 0.48 * (st.p_n2 + st.p_ch4) * st.pressure / p.gravity * std::pow(94.0 / std::max(t_mean, 94.0), 2);
        st.tau = tau_co2 + tau_h2o + tau_cia;
        double t_eq4 = S_EARTH * insol * (1.0 - st.albedo) / (4.0 * SIGMA);
        return std::pow(t_eq4 * (1.0 + 0.75 * st.tau) + (q + q_extra) / SIGMA, 0.25);
    };

    // The climate depends on history (CO2 in rock or in the air; a collapsed or a
    // thick atmosphere on a locked planet). Each run starts from an Earth-like
    // greenhouse, either with the ocean condensed and its CO2 in carbonates or
    // with every gas in the air.
    auto run_climate = [&](bool carbonate_start, bool frozen_start = false) {
    t = frozen_start ? 200.0 : 278.6 * std::pow(insol * 0.7, 0.25) * std::pow(1.626, 0.25);
    p_last = (inv_n2 + (carbonate_start ? 0.0 : inv_co2)) * keep_gas;
    tau_last = 0;
    for (int it = 0; it < 200; ++it) {
        double t_new = evaluate(t, -1.0, s);
        if (s.weathering) {  // bisect log10 pCO2 for 288 K
            double lo = -5.0, hi = std::log10(std::max(inv_co2, 1.1e-5));
            if (evaluate(t, std::pow(10.0, lo), s) > 288.0) hi = lo;
            else if (evaluate(t, std::pow(10.0, hi), s) < 288.0) lo = hi;
            for (int k = 0; k < 40 && hi - lo > 1e-4; ++k) {
                double mid = 0.5 * (lo + hi);
                (evaluate(t, std::pow(10.0, mid), s) > 288.0 ? hi : lo) = mid;
            }
            t_new = evaluate(t, std::pow(10.0, 0.5 * (lo + hi)), s);
        }
        // Runaway greenhouse: the ocean boils into the air, where UV splits it and
        // the hydrogen escapes within ~0.5 Gyr (Venus kept ~1e-5 of its water).
        // (Not a magma ocean's heat: its steam rains out again once a crust forms.)
        if (!icy && !runaway && water_kg > 0 && t_new > 500.0 && q_extra == 0) {
            runaway = true;
            if (age > 0.5) water_lost = 1e-5;
        }
        p_last = s.pressure;
        tau_last = s.tau;
        if (std::getenv("PHYSICS_DEBUG"))
            std::fprintf(stderr, "it %3d T %7.2f -> %7.2f  P %.3g co2 %.3g h2o %.3g A %.3f tau %.3g ocean %d weath %d eta %.2f dT %.1f\n",
                         it, t, t_new, s.pressure, s.p_co2, s.p_h2o, s.albedo, s.tau, s.ocean, s.weathering, s.eta, s.d_t);
        if (std::fabs(t_new - t) < 0.01) { t = t_new; break; }
        t = 0.5 * (t + t_new);
    }
    evaluate(t, s.weathering ? s.p_co2 : -1.0, s);
    };
    // 1. The young system (0.5 Gyr, fainter star, still spinning unless it locks
    //    faster): does the ocean condense, or run away and lose its water?
    bool young_ocean = false;
    if (water_kg > 0 && !icy) {
        insol = lum_at(std::min(0.5, age)) / (in.orbit_au * in.orbit_au);
        d_rad = p.lock_time_gyr < 0.5 && !moon ? d_rad_locked : d_rad_spin;
        q_extra = age <= 0.5 ? q_magma : 0.0;
        run_climate(true);
        young_ocean = s.ocean;
        insol = p.insolation;
        d_rad = locked_to_star ? d_rad_locked : d_rad_spin;
    }
    // 2. Today: CO2 in rock if there is still water, else in the air; if the
    //    ocean doesn't survive, CO2 builds up in the air again (no weathering).
    //    A snowball start keeps the ocean frozen only where that state is stable.
    q_extra = q_magma;
    if (in.snowball && young_ocean && water_kg * water_lost > 0 && !runaway) {  // (an ocean that froze)
        snowball_now = true;
        run_climate(true, true);
        snowball_now = s.frozen;
    }
    if (!snowball_now) {
        run_climate(water_kg * water_lost > 0 && !runaway);
        if (!s.ocean && water_kg * water_lost > 0 && !runaway) run_climate(false);
    }

    // --- Climate outputs ----------------------------------------------------------
    p.runaway = runaway;
    p.snowball = s.frozen;
    p.albedo = s.albedo;
    p.t_eq = 278.6 * std::pow(p.insolation * (1.0 - s.albedo), 0.25);
    p.t_mean = t;
    p.redistribution = s.eta;
    p.t_equator = t + s.d_t / 3.0;
    p.t_pole = std::max(t - 2.0 * s.d_t / 3.0, 20.0);
    p.t_winter_pole = std::min(p.t_equator, p.t_pole) * (1.0 - 0.5 * std::sin(obl) * (1.0 - s.eta));
    p.greenhouse_tau = s.tau;
    p.pressure_bar = s.pressure;
    p.life = s.life;
    p.oxygen = s.p_o2 > 0;
    if (p.life) pigment(p.star_teff, p.pigment_rgb);

    // Gases (partial pressures), mean molecular weight, scale height, Rayleigh.
    struct G3 { const char* n; double bar, mu, ray; };
    const G3 g3[] = {{"N2", s.p_n2, N2.mu, N2.rayleigh}, {"O2", s.p_o2, MU_O2, RAYLEIGH_O2},
                     {"CO2", s.p_co2, CO2.mu, CO2.rayleigh}, {"H2O", s.p_h2o, H2O.mu, H2O.rayleigh},
                     {"CH4", s.p_ch4, CH4.mu, CH4.rayleigh}};
    double mu_sum = 0, ray_sum = 0;
    for (const G3& g : g3)
        if (g.bar > 0) {
            p.gases.push_back({g.n, g.bar});
            mu_sum += g.bar * g.mu;
            ray_sum += g.bar * g.ray;
        }
    std::sort(p.gases.begin(), p.gases.end(), [](const Gas& l, const Gas& r) { return l.bar > r.bar; });
    if (s.pressure > 0) {
        p.mu = mu_sum / s.pressure;
        p.scale_height_km = R_GAS * t / (p.mu * 1e-3 * p.gravity) / 1e3;
        const double lam[3] = {650, 550, 450};
        for (int i = 0; i < 3; ++i)
            p.rayleigh_tau[i] = 0.0959 * (s.pressure / 1.013) * (9.81 / p.gravity) * (28.97 / p.mu) *
                                (ray_sum / s.pressure) * std::pow(550.0 / lam[i], 4);
    } else p.mu = 0, p.scale_height_km = 0;

    // Condensables: where and in what phase.
    auto phase = [&](const Species& sp, double t_loc) {
        return t_loc > sp.t_triple && s.pressure > sp.p_triple && t_loc < sp.t_crit ? "liquid" : "ice";
    };
    double t_cold = std::max(std::min(p.t_equator, p.t_pole), 20.0), t_warm = std::max(p.t_equator, p.t_pole);
    const std::string cold_place = locked_to_star ? " (night side)" : " (polar caps)";
    auto trapped = [&](const Species& sp, double gas, double cond) {  // what escape left
        double inv = gas + cond;
        Volatile v{sp.name, inv, gas, "gas"};
        if (inv <= 0) return;
        if (cond > 0) {
            bool global = p_sat(sp, t_warm) < inv;
            std::string ph = phase(sp, global ? t_warm : t_cold);
            v.state = ph == "liquid" ? "liquid (pools in lowlands)" : ph + (global ? " (global)" : cold_place);
        } else if (gas > p_sat(sp, p.t_winter_pole)) v.state = "gas + seasonal polar frost";
        p.volatiles.push_back(v);
    };
    if (icy) p.volatiles.push_back({"H2O", 0, 0, "ice crust (bulk)"});
    else if (water_kg * water_lost > 0) {
        double gel = water_kg * water_lost / (1000.0 * area);
        Volatile v{"H2O", column_bar(water_kg * water_lost), s.p_h2o, ""};
        v.state = s.ocean ? (s.ice_frac > 0 ? "liquid ocean + polar ice" : "liquid ocean")
                : s.frozen ? "ocean frozen over (snowball)"
                : runaway ? "vapour remnant (ocean lost to a runaway greenhouse)"
                : s.ice_frac > 0.5 ? "ice (global)" : s.ice_frac > 0 ? "ice" + cold_place : "vapour";
        p.ocean_depth_m = gel;
        p.volatiles.push_back(v);
    }
    trapped(CO2, s.weathering ? inv_co2 * keep_gas : s.p_co2, s.co2_cond);
    if (s.weathering) {
        p.volatiles.back().gas_bar = s.p_co2;
        p.volatiles.back().state = "gas (rest locked in carbonate rock)";
    }
    trapped(N2, s.p_n2, s.n2_cond);
    trapped(CH4, s.p_ch4, s.ch4_cond);

    p.ice_fraction = s.ice_frac;
    p.ocean_fraction = s.ocean_frac;
    if (s.ocean) { p.liquid = "water"; p.ocean_colour = p.life ? "deep blue, green where life blooms" : "deep blue"; }
    else if (s.frozen) { p.liquid = "water (frozen over)"; p.ocean_colour = "sea ice"; }
    else if (s.ch4_cond > 0 && t_warm > CH4.t_triple) {
        // Condensed methane beyond the vapour pressure pools in lowlands.
        double gel = s.ch4_cond * 1e5 / p.gravity / 450.0;
        p.ocean_depth_m = gel;
        p.ocean_fraction = gel / (gel + 1100.0 * std::sqrt(9.81 / p.gravity));
        p.liquid = p.ocean_fraction < 0.05 ? "methane lakes" : "methane seas";
        p.ocean_colour = "near-black";
    }

    // --- Geology ------------------------------------------------------------------
    if (in.regime != Regime::Auto) p.regime = in.regime;
    else if (icy) p.regime = q > 0.02 ? Regime::IceShellActive : Regime::IceShellInactive;
    else if (p.tidal_heat_wm2 > 1.0) p.regime = Regime::HeatPipe;
    else if (q < 0.015) p.regime = Regime::Dead;
    else if (s.weathering) p.regime = Regime::PlateTectonics;  // liquid water weakens the lithosphere
    else if (s.frozen && in.mass > 0.3 && q > 0.04) p.regime = Regime::PlateTectonics;  // (still liquid under the ice)
    else p.regime = Regime::StagnantLid;

    double volc = std::max(0.0, q - 0.015) / 0.065;  // Earth ~1
    switch (p.regime) {
        case Regime::PlateTectonics: p.volcanism = volc; p.volcanism_type = "basaltic ridges + explosive arcs"; break;
        case Regime::StagnantLid: p.volcanism = 0.5 * volc; p.volcanism_type = "basaltic shields and plains"; break;
        case Regime::HeatPipe: p.volcanism = q / 0.08; p.volcanism_type = "silicate + sulfur (Io-like)"; break;
        case Regime::IceShellActive: p.volcanism = std::max(0.0, q - 0.01) / 0.065; p.volcanism_type = "cryovolcanism (water / brine)"; break;
        default: p.volcanism = 0; p.volcanism_type = "none";
    }

    // Surface age: resurfacing by plates / volcanism / ice convection / erosion;
    // nothing older than ~0.5 Gyr after formation survives the early bombardment.
    double surf = age;
    if (p.regime == Regime::PlateTectonics) surf = 0.1;
    else if (p.regime == Regime::HeatPipe) surf = 0.001;
    else if (p.regime == Regime::IceShellActive) surf = 0.06;
    else if (p.regime == Regime::StagnantLid && p.volcanism > 0) surf = 0.25 / p.volcanism;
    bool erosion = (s.ocean || p.liquid != "none") && s.pressure > 0.01;
    if (erosion) surf = std::min(surf, 1.0);
    p.surface_age_gyr = std::max(0.0, std::min({surf, age - 0.5, p.melt_myr / 1000.0}));  // a magma ocean erases all
    // Impact flux falls off outside the inner system (fewer, slower impactors in
    // the Kuiper belt; ~(a / 1.5 AU)^-0.5, a guess).
    double flux = std::min(1.0, std::pow(in.orbit_au / 1.5, -0.5));
    p.crater_density = std::min(neukum(p.surface_age_gyr) / neukum(3.5) * flux, 10.0);  // saturates ~10x maria

    // Relief ~ strength / (rho g): Earth's ~20 km, ice ~0.3 x rock; small bodies
    // stay below ~1% of the radius (Mars 33 km vs Olympus Mons 22 + Hellas 8).
    p.max_relief_km = std::min(20.0 * 9.81 / p.gravity * (icy ? 0.3 : 1.0), 0.01 * p.radius_km);
    p.continental_fraction = p.regime == Regime::PlateTectonics ? 0.4 * std::min(1.0, age / 3.0) : 0.0;
    p.hypsometry = p.regime == Regime::PlateTectonics ? "bimodal: continents + ocean basins"
                 : p.regime == Regime::StagnantLid ? "unimodal: big shield volcanoes, maybe a hemispheric dichotomy"
                 : icy ? "low-relief ice shell" : "unimodal, cratered";
    if (icy && q > 0.001) {  // conductive ice shell over a subsurface ocean (k_ice ~2.5 W/m/K)
        double shell = 2.5 * (273.0 - std::min(t, 270.0)) / q;
        double rho_rock = (1 - w) / (f_core / 7900.0 + f_rock / 3300.0);  // the rock + iron interior
        double water_layer = radius - std::cbrt(3.0 * m_kg * (1 - w) / (4.0 * PI * rho_rock));
        if (shell < water_layer) p.ice_shell_km = shell / 1e3;
    }

    // Dynamo: plate tectonics cools the core fast enough to convect (Nimmo &
    // Stevenson 2000); a very large core keeps a weak field anyway (Mercury).
    p.dynamo = !icy && p.core_fraction > 0.1 && (p.regime == Regime::PlateTectonics || p.core_fraction > 0.5);
    p.field = !p.dynamo ? (age < 1.0 && !icy ? "early dynamo (fading)" : "none")
            : p.regime == Regime::PlateTectonics ? "strong (Earth-like)" : "weak (large core)";

    // --- Clouds and aerosols --------------------------------------------------------
    if (s.deck) {  // a steam atmosphere (over a magma ocean) condenses into water clouds aloft
        p.aerosols.push_back(s.p_h2o > 0.5 * s.pressure ? "water cloud deck over a steam atmosphere"
                                                         : "sulfuric acid cloud deck (pale yellow)");
        p.cloud_amount = 1.0;
    }
    if (s.haze) { p.aerosols.push_back("thick orange tholin haze"); p.cloud_amount = 1.0; }
    else if (s.p_ch4 > 0 && s.p_n2 > 0) p.aerosols.push_back("thin blue tholin haze layers");
    if (s.dust) p.aerosols.push_back("suspended dust (butterscotch sky)");
    if (s.ocean) { p.cloud_types.push_back("water"); p.cloud_amount = std::max(p.cloud_amount, 0.24); }
    // Steam too thin for a deck over ground too hot for an ocean still condenses
    // aloft (convective clouds; more steam, more cloud — a guess).
    if (!s.deck && !s.ocean && !s.frozen && s.t > 373.0 && s.p_h2o > 0.01) {
        p.cloud_types.push_back("water (convective, over hot ground)");
        p.cloud_amount = std::max(p.cloud_amount, std::clamp(0.25 + 0.15 * std::log10(s.p_h2o), 0.1, 0.7));
    }
    if (p.liquid.rfind("methane", 0) == 0) { p.cloud_types.push_back("methane (sparse)"); p.cloud_amount = std::max(p.cloud_amount, 0.03); }
    if (s.dust && p.ocean_depth_m > 0) { p.cloud_types.push_back("thin water/CO2 ice"); p.cloud_amount = std::max(p.cloud_amount, 0.02); }

    // Circulation: Hadley cell edge ~ sqrt(g H dT/T) / (Omega R) (Held & Hou 1980),
    // scaled so Earth's comes out at 30 deg.
    if (s.pressure > 1e-4) {
        double h_trop = 1.5 * p.scale_height_km * 1e3;
        double rel = p.gravity * h_trop * std::max(d_rad, 0.05) / std::pow(omega * radius, 2);
        double earth = 9.81 * 12.6e3 * 0.2 / std::pow(7.292e-5 * R_EARTH, 2);
        hadley_rad = 30.0 * std::sqrt(rel / earth);
        p.hadley_deg = std::min(hadley_rad, 90.0);
        p.cells_per_hemisphere = std::max(1, static_cast<int>(std::lround(90.0 / hadley_rad)));
        if (locked_to_star) { p.cells_per_hemisphere = 0; p.circulation = "day-to-night flow (tidally locked)"; }
        else if (hadley_rad >= 60) p.circulation = "one Hadley cell per hemisphere (slow rotator, possible superrotation)";
        else {
            char buf[96];
            std::snprintf(buf, sizeof buf, "%d cells per hemisphere, Hadley edge %.0f deg, %d jet%s",
                          p.cells_per_hemisphere, hadley_rad, p.cells_per_hemisphere - 1, p.cells_per_hemisphere == 2 ? "" : "s");
            p.circulation = buf;
        }
    } else { p.cells_per_hemisphere = 0; p.circulation = "none (no atmosphere)"; }

    // --- Surface materials ------------------------------------------------------------
    bool airless = s.pressure < 1e-6;
    bool tholins = inv_ch4 > 0;
    if (icy) {
        p.materials.push_back("water-ice bedrock");
        for (const Volatile& v : p.volatiles)
            if ((v.name == "N2" || v.name == "CH4") && v.state.rfind("ice", 0) == 0) p.materials.push_back(v.name + " ice");
        if (tholins) p.materials.push_back(s.haze ? "organic (tholin) sand dunes" : "tholins (red-brown)");
        if (p.regime == Regime::IceShellActive) p.materials.push_back(moon ? "salts tinted by irradiation (red-brown cracks)" : "fresh brine deposits");
        if (airless && p.surface_age_gyr > 1.0) p.materials.push_back("space-weathered dark lag");
    } else {
        if (p.regime == Regime::PlateTectonics) p.materials.push_back("basalt seafloor, granite continents");
        else if (p.regime == Regime::HeatPipe) p.materials.push_back("sulfur and SO2 frost (yellow/orange/red), fresh lava");
        else p.materials.push_back("basalt");
        if (airless) p.materials.push_back("space-weathered regolith (dark grey)");
        if (airless && p.crater_density > 0.5) p.materials.push_back("bright rays of young craters");
        // Iron rusts with liquid water (an early ocean) or with the oxidants left
        // when UV splits water vapour and the hydrogen escapes (Mars); a world whose
        // water was always frozen out of the air stays basalt.
        const bool oxidised = young_ocean || s.ocean || s.p_h2o > 1e-7;
        if (w > 1e-6 && t < 500 && !airless && oxidised && s.ice_frac < 0.99) {  // (not seen under global ice)
            // How far: an ocean (then or now) oxidises the iron fully; vapour alone by
            // the oxidants its photolysis leaves (H escapes), so by vapour x time: Mars'
            // 6e-6 bar over 4.6 Gyr rusted its dust fully, 1e-7 bar for 0.5 Gyr leaves
            // it grey-brown (full from 1e-5 bar Gyr, 0.25 at 1e-8). Which oxide:
            // with liquid water today goethite (ochre) forms; dry or hot (> ~350 K,
            // goethite dehydrates) it is hematite (Mars' red dust is dry UV oxidation,
            // whatever water came before).
            p.rust_ox = young_ocean || s.ocean ? 1.0
                      : std::clamp(0.25 + 0.25 * (std::log10(std::max(s.p_h2o * age, 1e-15)) + 8.0), 0.25, 1.0);
            p.rust_wet = s.ocean ? std::clamp((400.0 - t) / 50.0, 0.0, 1.0) : 0.0;
            const char* kind = p.rust_ox < 0.7 ? "weakly oxidised, grey-brown" : p.rust_wet > 0.5 ? "goethite, ochre" : "hematite, red";
            p.materials.push_back(p.oxygen ? std::string("soils, iron oxides in deserts")
                                           : std::string("iron-oxide dust (rust: ") + kind + ")");
        }
        if (s.ocean) p.materials.push_back("salt flats in arid basins");
        if (s.dust || (s.ocean && s.pressure > 0.1)) p.materials.push_back("sand dunes");
        if (p.life) p.materials.push_back("vegetation");
        if (s.ice_frac > 0) p.materials.push_back("water ice");
        if (!s.weathering && (s.co2_cond > 0 || (s.p_co2 > 0 && s.p_co2 > p_sat(CO2, p.t_winter_pole)))) p.materials.push_back("CO2 frost");
        if (q > 0.3 || t > 1000 || p.melt > 0) p.materials.push_back("glowing lava");
    }
    return p;
}

void describe(const PhysicalInputs& in, const PlanetPhysics& p) {
    std::printf("=== %s ===\n", in.name.c_str());
    // Machine-readable inputs (tools/prefs.py: generation parameters -> preference)
    std::printf("inputs     star_mass=%.6g age_gyr=%.6g orbit_au=%.6g mass=%.6g composition=%d volatiles=%.6g "
                "day_hours=%.6g obliquity=%.6g moons=%d moon_mass_ratio=%.6g host_mass_kg=%.6g host_km=%.6g "
                "impact_myr=%.6g snowball=%d\n",
                in.star_mass, in.age_gyr, in.orbit_au, in.mass, static_cast<int>(in.composition), in.volatile_fraction,
                in.day_hours, in.obliquity_deg, in.moons, in.moon_mass_ratio, in.host_mass_kg, in.host_distance_km,
                in.impact_myr_ago, in.snowball ? 1 : 0);
    std::printf("star       %.2f Msun, L %.3f Lsun, Teff %.0f K (rgb %.2f %.2f %.2f), lifetime %.1f Gyr%s\n",
                in.star_mass, p.star_lum, p.star_teff, p.star_rgb[0], p.star_rgb[1], p.star_rgb[2],
                p.star_lifetime_gyr, in.age_gyr > p.star_lifetime_gyr ? "  [star has left the main sequence!]" : "");
    std::printf("orbit      %.3f AU%s, insolation %.4g Earth, XUV fluence %.3g Earth\n", in.orbit_au,
                in.host_mass_kg > 0 ? " (host planet)" : "", p.insolation, p.xuv_rel);
    std::printf("bulk       %.4g M_E, core %.2f, volatiles %.2g -> R %.0f km, rho %.0f kg/m3, g %.2f m/s2, v_esc %.2f km/s\n",
                in.mass, p.core_fraction, in.volatile_fraction, p.radius_km, p.density, p.gravity, p.v_esc_kms);
    std::printf("rotation   %.1f h%s (lock time %.3g Gyr), obliquity %.1f deg\n", p.rotation_hours,
                p.tidally_locked ? " TIDALLY LOCKED" : "", p.lock_time_gyr, in.obliquity_deg);
    std::printf("interior   heat flow %.3g W/m2 (tidal %.3g) -> %s, volcanism %.2f x Earth (%s)\n",
                p.heat_flux_wm2, p.tidal_heat_wm2, regime_name(p.regime), p.volcanism, p.volcanism_type.c_str());
    if (p.rift > 0)
        std::printf("           young crust over a magma ocean (closed %.3g Myr ago): %.1f%% molten along cracks\n",
                    p.melt_myr - p.magma_tau_myr, 100 * p.rift);
    if (p.melt > 0)
        std::printf("           magma ocean (%s %.3g Myr ago): %.0f%% still molten, crust closes in ~%.2g Myr\n",
                    in.impact_myr_ago >= 0 && in.impact_myr_ago < in.age_gyr * 1000.0 ? "giant impact" : "formed",
                    p.melt_myr, 100 * p.melt, p.magma_tau_myr);
    std::printf("           field: %s; surface age %.3g Gyr, craters %.3g x lunar maria\n", p.field.c_str(),
                p.surface_age_gyr, p.crater_density);
    std::printf("           relief up to ~%.0f km, %s", p.max_relief_km, p.hypsometry.c_str());
    if (p.continental_fraction > 0) std::printf(", continental crust %.0f%%", 100 * p.continental_fraction);
    if (p.ice_shell_km > 0) std::printf(", ice shell %.0f km over a subsurface ocean", p.ice_shell_km);
    std::printf("\n");
    std::printf("escape     %.3g x shoreline-normalised (Earth 1; >4 loses air)\n", p.escape_ratio);
    std::printf("atmosphere %.3g bar", p.pressure_bar);
    for (const Gas& g : p.gases) std::printf("  %s %.3g", g.name.c_str(), g.bar);
    if (p.pressure_bar > 0) std::printf("\n           mu %.1f, scale height %.1f km, Rayleigh tau 650/550/450 nm %.3g/%.3g/%.3g",
                                        p.mu, p.scale_height_km, p.rayleigh_tau[0], p.rayleigh_tau[1], p.rayleigh_tau[2]);
    std::printf("\n");
    std::printf("aerosols  ");
    if (p.aerosols.empty()) std::printf(" none");
    for (const std::string& a : p.aerosols) std::printf(" [%s]", a.c_str());
    std::printf("; clouds:");
    if (p.cloud_types.empty()) std::printf(" none");
    for (const std::string& c : p.cloud_types) std::printf(" [%s]", c.c_str());
    std::printf(" (mean opacity %.2f)\n", p.cloud_amount);
    std::printf("climate    albedo %.2f, T_eq %.0f K, greenhouse tau %.3g -> T %.0f K (%.0f C)%s\n", p.albedo, p.t_eq,
                p.greenhouse_tau, p.t_mean, p.t_mean - 273.15, p.runaway ? "  [runaway greenhouse happened]" : "");
    std::printf("           %s %.0f K, %s %.0f K, winter pole %.0f K; redistribution %.2f\n",
                p.cells_per_hemisphere == 0 && p.circulation[0] == 'd' ? "dayside" : "equator", p.t_equator,
                p.cells_per_hemisphere == 0 && p.circulation[0] == 'd' ? "nightside" : "pole", p.t_pole,
                p.t_winter_pole, p.redistribution);
    std::printf("           circulation: %s\n", p.circulation.c_str());
    std::printf("liquids    %s", p.liquid.c_str());
    if (p.liquid != "none") std::printf(" (%.0f m global layer, %.1f%% of the surface, %s)", p.ocean_depth_m,
                                        100 * p.ocean_fraction, p.ocean_colour.c_str());
    std::printf("; ice cover %.0f%%\n", 100 * p.ice_fraction);
    for (const Volatile& v : p.volatiles)
        std::printf("           %-4s inventory %-9.3g bar  air %-9.3g bar  %s\n", v.name.c_str(), v.inventory_bar,
                    v.gas_bar, v.state.c_str());
    std::printf("surface   ");
    for (const std::string& m : p.materials) std::printf(" [%s]", m.c_str());
    std::printf("\n");
    std::printf("life       %s", p.life ? "yes" : "no");
    if (p.life) std::printf(", pigment rgb %.2f %.2f %.2f%s", p.pigment_rgb[0], p.pigment_rgb[1], p.pigment_rgb[2],
                            p.oxygen ? ", oxygen atmosphere" : "");
    if (in.civilisation) std::printf(", civilisation (city lights)");
    std::printf("\n");
}

WorldSpec world_from_physics(const PhysicalInputs& in, const PlanetPhysics& p, PlanetParams& prm) {
    WorldSpec w;
    auto has = [](const std::vector<std::string>& v, const char* key) {
        for (const std::string& s : v)
            if (s.find(key) != std::string::npos) return true;
        return false;
    };
    const bool icy = in.composition == Composition::IceRock;
    switch (p.regime) {
        case Regime::PlateTectonics: w.style = TerrainStyle::Plates; break;
        case Regime::StagnantLid: case Regime::HeatPipe: w.style = TerrainStyle::Stagnant; break;
        case Regime::Dead: w.style = TerrainStyle::Dead; break;
        default: w.style = TerrainStyle::IceShell;
    }
    w.ocean = p.liquid.rfind("water", 0) == 0;  // (a frozen-over ocean too: the climate puts sea ice on it)
    prm.land_fraction = w.ocean ? static_cast<float>(1.0 - p.ocean_fraction) : 1.0f;
    w.relief = static_cast<float>(p.max_relief_km / 20.0 * EARTH_RADIUS_KM / p.radius_km);
    w.volcanism = static_cast<float>(p.volcanism);
    w.cracks = p.regime == Regime::IceShellActive;
    for (const Volatile& v : p.volatiles)
        if (v.name == "N2" && v.state.rfind("ice", 0) == 0) w.glacier_basin = true;
    // Craters: the derived density is the average surface. Old crust (as old as
    // the crust can be, ~age - 0.5 Gyr) carries the full record; resurfaced plains
    // keep only craters younger than themselves.
    {
        double flux = std::min(1.0, std::pow(in.orbit_au / 1.5, -0.5));
        double oldest = std::min(in.age_gyr - 0.5, p.melt_myr / 1000.0);  // (a magma ocean erases the record)
        double old_density = std::min(neukum(std::max(0.0, oldest)) / neukum(3.5) * flux, 10.0);
        old_density = std::max(old_density, p.crater_density);
        w.crater_density = w.style == TerrainStyle::Plates ? 0.0f : static_cast<float>(old_density);
        // (plains are at least ~0.3 Gyr younger than the average surface)
        w.crater_density_young = static_cast<float>(std::min(p.crater_density,
            neukum(std::max(0.0, p.surface_age_gyr - 0.3)) / neukum(3.5) * flux));
    }
    w.erosion = w.ocean;
    w.radius_km = static_cast<float>(p.radius_km);
    // Ice flows over Gyr (viscous relaxation) and volatile frost blankets
    // relief: craters on icy bodies stay shallower.
    if (icy) w.crater_relief *= 0.85f;

    // Climate: shift Earth's curve (ocean worlds) or the physics' P2 profile.
    w.t_offset = static_cast<float>(p.t_mean - 288.0);
    w.t_equator = static_cast<float>(p.t_equator - 273.15);
    w.t_pole = static_cast<float>(p.t_pole - 273.15);
    w.locked = p.tidally_locked && in.host_mass_kg <= 0;  // to its star: day / night side
    w.jet_lat = static_cast<float>(std::min(1.5 * p.hadley_deg, 70.0));

    // Light: star colour white-balanced to the Sun.
    double sun[3];
    blackbody_rgb(5772.0, sun);
    double tint[3], mx = 0;
    for (int i = 0; i < 3; ++i) { tint[i] = p.star_rgb[i] / sun[i]; mx = std::max(mx, tint[i]); }
    w.star_color = Vec3(static_cast<float>(tint[0] / mx), static_cast<float>(tint[1] / mx), static_cast<float>(tint[2] / mx));

    // Air optics relative to Earth's (Rayleigh tau 0.0935 at 550 nm, scale
    // height 8.5 km of 6371). Very thick air is capped: single scattering
    // can't do optically thick, and decks hide the ground anyway.
    w.optics.enabled = p.pressure_bar > 1e-4;
    w.optics.rayleigh = static_cast<float>(std::min(p.rayleigh_tau[1] / 0.0935, 3.0));
    w.optics.height = static_cast<float>((p.scale_height_km / p.radius_km) / (8.5 / 6371.0));
    w.optics.mie = static_cast<float>(std::clamp(p.pressure_bar, 0.3, 2.0));
    if (has(p.aerosols, "dust")) {
        // Dust is forward-scattering (g ~0.6), but it scatters many times, which
        // single scattering misses: a flatter phase and 1.5x the haze over the
        // ground stand in for it. The glow off the limb stays thin (photos show
        // none; the exaggerated shell read as an unnaturally thick atmosphere).
        w.optics.mie = 2.0f; w.optics.mie_albedo = Vec3(1.0f, 0.72f, 0.45f);
        w.optics.mie_g = 0.2f; w.optics.haze = 1.5f; w.optics.rim = 0.3f;
    }
    if (has(p.aerosols, "tholin haze")) { w.optics.mie = 2.0f; w.optics.mie_albedo = Vec3(1.0f, 0.70f, 0.35f); }
    if (has(p.aerosols, "sulfuric")) { w.optics.mie = 2.0f; w.optics.mie_albedo = Vec3(1.0f, 0.97f, 0.90f); }

    // Clouds. Decks: the weather sim still runs (at half cover) and only shades the deck.
    prm.cloud_amount = static_cast<float>(p.cloud_amount);
    if (has(p.aerosols, "sulfuric acid cloud deck")) {
        w.deck = true;
        w.deck_light = Vec3(0.92f, 0.91f, 0.90f);   // near-neutral whites (photos), faint warm
        w.deck_dark = Vec3(0.79f, 0.76f, 0.72f);
        w.deck_texture = 2.0f;  // convective sulfuric clouds: streaks/swirls (tools/tune.py)
    } else if (has(p.aerosols, "steam atmosphere")) {
        w.deck = true;
        w.deck_light = Vec3(0.94f, 0.94f, 0.95f);  // water cloud tops: white
        w.deck_dark = Vec3(0.80f, 0.81f, 0.84f);
        w.deck_texture = 2.0f;  // convective, like Venus' deck
    } else if (has(p.aerosols, "thick orange tholin haze")) {
        w.deck = true;
        w.deck_light = Vec3(0.91f, 0.80f, 0.51f);  // (vibrance-compensated photo colours)
        w.deck_dark = Vec3(0.88f, 0.73f, 0.45f);
        w.deck_texture = 0.2f;  // photochemical haze is smooth (no convective cloud texture)
    }
    if (w.deck) {
        // Seasons tilt the deck: one hemisphere darker (Titan's northern hood).
        w.deck_asym = static_cast<float>(0.5 * std::fabs(std::sin(in.obliquity_deg * PI / 180.0)));
        prm.cloud_amount = 0.5f;
        // Only the air above the deck scatters, and the glow there is aerosol
        // (photos show no blue halo around Venus or Titan).
        w.optics.rayleigh = std::min(w.optics.rayleigh, 0.15f);
    }

    // Surface materials. Colours measured on the reference photos with
    // tools/endmembers.py (dark / median / bright end of the disk's colour
    // line, median over photos), desaturated by the 1.3x vibrance grade the
    // renders get (references don't).
    w.biomes = p.life;
    if (icy) {
        w.rock = Vec3(0.84f, 0.82f, 0.77f);    // water ice
        w.dark = Vec3(0.66f, 0.62f, 0.56f);
        w.bright = Vec3(0.92f, 0.92f, 0.90f);
        w.dark_amount = 0.1f;
        const bool brine = has(p.materials, "salts") || has(p.materials, "brine");
        if (brine) { w.stain = Vec3(0.56f, 0.44f, 0.32f); w.stain_amount = 0.68f; }  // red-brown share of europa.jpg (30 %)
        w.alt = Vec3(0.60f, 0.66f, 0.58f);     // grey-green ice (older, radiation-processed)
        if (has(p.materials, "tholins")) {
            w.rock = Vec3(0.80f, 0.76f, 0.69f);
            w.stain = Vec3(0.33f, 0.25f, 0.19f);  // tholin-rich dark terrain
            w.stain_amount = 0.9f;  // tools/tune.py vs 3 photos
        }
        w.ice = Vec3(0.94f, 0.90f, 0.82f);     // N2 / CH4 ice
        // Physical colour model (WorldSpec::absorber_floor): clean ice + an absorber
        // column from the processes. Material constants: clean fine-grained ice is
        // faintly blue (water ice absorbs a little red); tholin and radiolysed
        // sulfur salts absorb blue most, saturating at their own dark colour
        // (fitted to the New Horizons / Galileo true-colour photos).
        const bool tholins = has(p.materials, "tholins"), lag = has(p.materials, "dark lag");
        if (tholins || brine || lag) {
            w.true_color = true;
            w.ice_clean = Vec3(0.95f, 0.95f, 0.96f);
            if (tholins) {  // haze fallout (CH4 photolysis) over Gyr on exposed ground
                w.absorber_k = Vec3(1.0f, 1.6f, 2.4f);
                w.absorber_floor = Vec3(0.24f, 0.17f, 0.11f);
                w.fallout = has(p.aerosols, "tholin haze") ? static_cast<float>(3.0 * std::min(1.0, p.surface_age_gyr / 4.0)) : 0.0f;
                // Volatiles leave the latitudes that get the most sun over the
                // seasons; at high obliquity that is the band inside the arctic
                // circles (90 - obliquity, folded: Pluto ~30 deg).
                double ob = std::min(in.obliquity_deg, 180.0 - in.obliquity_deg);
                w.volatile_band = static_cast<float>(90.0 - ob);
                // Without a haze, CH4 ice reddens in place (UV, cosmic rays: tholins
                // over ~Gyr, the red of Charon's pole and of old Kuiper-belt surfaces).
                if (w.fallout == 0.0f) w.weathering = static_cast<float>(std::min(1.0, p.surface_age_gyr / 2.0));
                if (brine && w.cracks) w.brine = 1.3f;  // erupted salts stain chaos and lineae too (same absorber family)
            } else if (brine) {  // sulfur salts: implanted plasma + endogenic brine
                w.ice_clean = Vec3(0.86f, 0.89f, 0.93f);
                w.coarse_ice = Vec3(0.20f, 0.12f, 0.05f);  // water-ice surface
                w.absorber_k = Vec3(1.0f, 1.4f, 2.0f);
                w.absorber_floor = Vec3(0.42f, 0.26f, 0.15f);
                // plasma density falls off with distance from the host (~r^-2, Europa = 1)
                const bool giant_host = in.host_mass_kg > 10.0 * 5.97e24;
                w.trailing = giant_host ? static_cast<float>(0.35 * std::min(1.0, std::pow(6.71e5 / in.host_distance_km, 2.0))) : 0.0f;
                w.brine = w.cracks ? 1.3f : 0.0f;
            } else {  // dark lag: rock and organic dust, nearly grey, faintly brown (Callisto)
                w.ice_clean = Vec3(0.86f, 0.89f, 0.93f);
                w.coarse_ice = Vec3(0.20f, 0.12f, 0.05f);
                w.absorber_k = Vec3(1.0f, 1.15f, 1.35f);
                w.absorber_floor = Vec3(0.20f, 0.17f, 0.14f);
            }
            // Old airless ice darkens: sublimation leaves a lag of the rock / organic
            // dust mixed into the ice and micrometeorites garden it in (Callisto,
            // Ganymede's dark terrain, albedo ~0.2), in patches (terrain units);
            // fresh ejecta (bright rays) and cold crests, where the sublimated ice
            // condenses again, stay bright.
            if (lag) {
                w.weathering = std::max(w.weathering, static_cast<float>(1.2 * std::min(1.0, p.surface_age_gyr / 3.0)));
                w.dark_amount = 0.35f;
                w.crest_albedo = 0.6f;
            }
        }
    } else if (has(p.materials, "space-weathered")) {
        w.dark_amount = 0.1f;
        w.rock = Vec3(0.52f, 0.52f, 0.52f);    // neutral grey (photos)
        w.dark = Vec3(0.39f, 0.39f, 0.39f);
        w.bright = Vec3(0.80f, 0.80f, 0.80f);
        w.alt = Vec3(0.56f, 0.52f, 0.46f);     // warmer, less weathered plains
    } else if (has(p.materials, "rust")) {
        // The Mars photo's colour distribution only for Mars itself (calibration to
        // its reference photo); every other rusty world takes its oxide's colours.
        if (in.name == "mars") w.color_profile = "data/profiles/mars.prof";
        // Hematite: matched on the paletted renders vs the original Mars photo
        // (tools/endmembers.py: dark 0.31 0.21 0.18 / median 0.56 0.33 0.25 / bright 0.70 0.41 0.29).
        // Goethite: ochre (yellow-brown, Earth's wetter deserts and laterites);
        // unoxidised: basalt. Mixed by hydration, then by degree of oxidation.
        auto oxide = [&](Vec3 hem, Vec3 goe, Vec3 bas) {
            Vec3 ox = hem + (goe - hem) * static_cast<float>(p.rust_wet);
            return bas + (ox - bas) * static_cast<float>(p.rust_ox);
        };
        w.rock = oxide(Vec3(0.70f, 0.41f, 0.30f), Vec3(0.70f, 0.55f, 0.34f), Vec3(0.33f, 0.30f, 0.28f));    // dust (Mars: brick red)
        w.dark = oxide(Vec3(0.31f, 0.21f, 0.18f), Vec3(0.30f, 0.26f, 0.20f), Vec3(0.22f, 0.21f, 0.20f));    // basaltic sand
        w.bright = oxide(Vec3(0.80f, 0.48f, 0.37f), Vec3(0.80f, 0.66f, 0.45f), Vec3(0.50f, 0.48f, 0.45f));
        w.dark_amount = 0.45f;  // user pick A4 was 0.55; less dark area (user, 2026-11-04)
        w.alt = oxide(Vec3(0.66f, 0.50f, 0.48f), Vec3(0.66f, 0.58f, 0.46f), Vec3(0.40f, 0.38f, 0.36f));   // fine dust (Mars: pinkish)
    } else if (has(p.materials, "sulfur")) {
        w.rock = Vec3(0.80f, 0.70f, 0.35f);
        w.dark = Vec3(0.30f, 0.22f, 0.15f);
        w.bright = Vec3(0.90f, 0.85f, 0.60f);
    } else {
        w.rock = Vec3(0.33f, 0.30f, 0.28f);    // basalt
        w.dark = Vec3(0.22f, 0.21f, 0.20f);
        w.bright = Vec3(0.50f, 0.48f, 0.45f);
    }
    // Water-ice caps on dry worlds: the cap edge is the temperature that makes
    // the colder area (P2 latitude profile) equal the derived ice cover.
    if (!w.ocean && !icy && p.ice_fraction > 0) {
        w.ice = Vec3(0.93f, 0.92f, 0.90f);
        double dt = p.t_equator - p.t_pole, sc = 1.0 - std::min(p.ice_fraction, 1.0);  // sin(lat) of the edge
        w.ice_below = static_cast<float>(p.t_mean - 273.15 + dt * (1.0 / 3.0 - sc * sc));
        // global ice has no edge (the edge's +-3 K blend and noise left the warmest cells bare)
        if (p.ice_fraction >= 0.99) w.ice_below = static_cast<float>(p.t_equator - 273.15 + 20.0);
    }
    // Lifeless ocean worlds (the Earth climate, shifted): land ice where Earth has
    // it, annual mean below ~-14 C (warm summers melt the rest).
    if (w.ocean && !w.biomes) { w.ice = Vec3(0.93f, 0.92f, 0.90f); w.ice_below = -14.0f; }
    if (!w.biomes) {
        // Photos: best fits 0.55-0.95 per body (exposures differ); bright decks
        // (and a rocky world under global ice) are photographed near white.
        w.auto_p95 = w.deck || (!icy && p.ice_fraction > 0.9) ? 0.9f : 0.72f;
        // Sharp unit contacts fit Pluto's photos; chaos (ice shell) and airless
        // regolith (gardened by impacts) stay soft (tools/tune.py).
        w.edge = (w.cracks || !w.optics.enabled) ? 1.0f : 0.2f;
        // Dark regolith scatters once (Lommel-Seeliger, flat full disk); bright
        // ice scatters many times (closer to Lambert).
        // An atmosphere (dust haze, extinction) darkens the limb again.
        w.lommel_seeliger = static_cast<float>(std::clamp(1.0 - 0.6 * p.albedo, 0.0, 1.0) * (w.optics.enabled ? 0.6 : 1.0));
        bool airless = !w.optics.enabled, dusty = has(p.aerosols, "dust");
        if (w.style == TerrainStyle::Stagnant || w.style == TerrainStyle::Dead) w.scarps = 1.0f;
        if (w.style == TerrainStyle::Stagnant && w.volcanism > 0.01f) w.canyons = 1.0f;
        if (w.style == TerrainStyle::Stagnant) w.ground_detail = 0.5f;  // stronger read as crumpled paper (vs mars.jpg)
        if (dusty) {  // user picks: streaks B4 (3 x 4; strength 2 now that old crust has ~10x
                      // more craters), dark contrast A3; the rest tools/tune.py vs the original photo
            w.streaks = 3.0f; w.streak_strength = 1.0f; w.dust = 1.0f; w.dark_mix = 1.0f;
            w.sand_lows = 1.2f;      // user pick A4
            w.crater_fill = 1.5f;    // wind (and once water) filled old craters: flat floors, worn rims (user pick A3)
            w.ejecta_lobes = 1.0f;   // ground ice: rampart ejecta (dusty worlds are the ones with a frozen past here)
            w.lommel_seeliger = 0.9f;  // the photos stay bright nearly to the limb (dust scatters once)
            w.fresh = 0.01f;         // dust blankets young ejecta and rays within ~Myr
            w.dust_texture = 0.0f;   // the wind shapes regions; its swirls are not seen in the photos
            w.relief_light = 1.0f; w.relief_sun = 40.0f; w.shading = 1.0f;  // mosaic-style relief (user pick B1;
                                     // shading 1.5 -> 1 once the mosaic light was flattened: relief outweighed albedo)
            w.dark_level = 0.8f;     // albedo, not shadows, gives the photo's darkest tones
            w.saturation = 0.85f;
            w.auto_p95 = 0.47f;  // brick-red, not pastel (matched on the original Mars photo)
        }
        if (airless) {
            w.slope_albedo = 0.3f;
            w.fresh = 0.2f;  // small young craters keep bright ejecta: Mercury's speckle (tools/tune.py)
        }
    }
    // Seasonal frost cap: a condensable gas whose frost point the polar night can
    // reach (thin air: little heat transport) frosts the winter pole down to
    // ~10 deg past the polar-night circle (90 - obliquity; the low winter sun
    // can't keep the ground warm either: Mars' seasonal cap reaches ~50-55 deg),
    // less with heat transport.
    for (const Volatile& v : p.volatiles)
        if (v.state.find("seasonal") != std::string::npos && std::min(in.obliquity_deg, 180.0 - in.obliquity_deg) > 5.0)
            w.frost_lat = static_cast<float>(90.0 - std::min(in.obliquity_deg, 180.0 - in.obliquity_deg)  // (folded: retrograde spins)
                                             - 10.0 * (1.0 - p.redistribution)
                                             + 25.0 * p.redistribution);
    // Lava (magma ocean, or a day side hotter than rock can stay solid): the
    // renderer finds the molten share from each cell's temperature and draws its
    // thermal glow (Planck at ~1300-1700 K) relative to the starlight here:
    // `thermal` = 1 / (white ground in this star's light, in units of the Sun's at
    // 1 AU; (R_sun / AU)^2 = 2.16e-5), at the brightest channel the renderer's star
    // colour is normalised to.
    w.lava = !icy && (std::max(p.t_equator, p.t_pole) > CRUST_K || p.rift > 0);
    w.lava_rift = static_cast<float>(p.rift);
    // A locked lava world with (nearly) no air: rock vapour from the day-side lava
    // sea condenses on cooler ground toward the terminator (Kite et al. 2016).
    if (w.lava && w.locked && p.pressure_bar < 0.01 && p.t_equator > LAVA_K) w.mineral_frost = 1.0f;
    // Wind moves snow on ice-covered rocky worlds with air (snow_drift).
    if (!icy && p.ice_fraction > 0.5 && p.pressure_bar > 0.005) w.snow_drift = 1.0f;
    if (w.lava) {
        Vec3 b = planck_rel(static_cast<float>(p.star_teff));
        double m = std::max({b.x, b.y, b.z});
        w.thermal = static_cast<float>(1.0 / (2.16e-5 * p.insolation * m * std::pow(5772.0 / p.star_teff, 4)));
    }
    w.lights = in.civilisation;
    return w;
}

// --- Presets ------------------------------------------------------------------------
// Real inputs only (mass, orbit, composition class, rotation, ...); volatile
// fraction: Earth's ocean mass fraction for all three terrestrial planets with
// air (same endowment, different fates), measured ice fractions for the moons.
namespace {
struct Preset {
    PhysicalInputs in;
    // Real values to check against.
    double r_km, g, t_k, p_bar, albedo, surface_age_gyr;
    const char *liquid, *regime, *field;
};

std::vector<Preset> presets() {
    std::vector<Preset> v;
    auto add = [&](PhysicalInputs in, double r, double g, double t, double pb, double alb, double sa,
                   const char* liq, const char* reg, const char* fld) {
        v.push_back({in, r, g, t, pb, alb, sa, liq, reg, fld});
    };
    PhysicalInputs i;
    i = {}; i.name = "mercury"; i.orbit_au = 0.387; i.mass = 0.0553; i.composition = Composition::IronRich;
    i.volatile_fraction = 0; i.day_hours = 1407.6; i.obliquity_deg = 0.03; i.moons = 0; i.moon_mass_ratio = 0;
    add(i, 2440, 3.70, 440, 0, 0.088, 4.0, "none", "stagnant lid (dead)", "weak");
    i = {}; i.name = "venus"; i.orbit_au = 0.723; i.mass = 0.815; i.day_hours = 5832.5; i.obliquity_deg = 177.4;
    i.moons = 0; i.moon_mass_ratio = 0;
    add(i, 6052, 8.87, 737, 92, 0.76, 0.5, "none", "stagnant lid (episodic?)", "none");
    i = {}; i.name = "earth"; i.civilisation = true;
    add(i, 6371, 9.81, 288, 1.013, 0.306, 0.1, "water, 71%", "plate tectonics", "strong");
    i = {}; i.name = "mars"; i.orbit_au = 1.524; i.mass = 0.107; i.core_fraction = 0.24; i.day_hours = 24.62;
    i.obliquity_deg = 25.2; i.moons = 2; i.moon_mass_ratio = 1.7e-8;
    add(i, 3390, 3.71, 210, 0.006, 0.25, 3.5, "none (H2O caps, seasonal CO2)", "stagnant lid", "none (remanent)");
    i = {}; i.name = "europa"; i.orbit_au = 5.20; i.mass = 0.008; i.composition = Composition::IceRock;
    i.volatile_fraction = 0.08; i.day_hours = 85.2; i.obliquity_deg = 0.1; i.moons = 0; i.moon_mass_ratio = 0;
    i.host_mass_kg = 1.898e27; i.host_distance_km = 671100; i.host_eccentricity = 0.009;
    add(i, 1561, 1.31, 102, 1e-12, 0.68, 0.06, "subsurface ocean", "ice shell, active", "induced only");
    i = {}; i.name = "titan"; i.orbit_au = 9.58; i.mass = 0.0225; i.composition = Composition::IceRock;
    i.volatile_fraction = 0.45; i.day_hours = 382.7; i.obliquity_deg = 26.7; i.moons = 0; i.moon_mass_ratio = 0;
    i.host_mass_kg = 5.683e26; i.host_distance_km = 1221870; i.host_eccentricity = 0.0288;
    add(i, 2575, 1.35, 94, 1.47, 0.27, 0.5, "methane lakes (polar) + sub-ice ocean", "ice shell", "none");
    i = {}; i.name = "pluto"; i.orbit_au = 39.5; i.mass = 0.00218; i.composition = Composition::IceRock;
    i.volatile_fraction = 0.32; i.day_hours = 153.3; i.obliquity_deg = 119.6; i.moons = 5; i.moon_mass_ratio = 0.122;
    add(i, 1188, 0.62, 40, 1e-5, 0.72, 4.0, "none (N2/CH4 ice; sub-ice ocean likely)", "ice shell (N2 glaciers)", "none");
    return v;
}
}  // namespace

bool planet_preset(const std::string& name, PhysicalInputs& in) {
    for (const Preset& p : presets())
        if (p.in.name == name) { in = p.in; return true; }
    return false;
}

std::vector<std::string> preset_names() {
    std::vector<std::string> n;
    for (const Preset& p : presets()) n.push_back(p.in.name);
    return n;
}

// A new planet: inputs drawn from plausible ranges, then derive() decides the rest.
// Insolation is drawn instead of the orbit (log-uniform: scorched to frozen, 40%
// of planets in the habitable zone, the rarest and richest class), so every
// class of world comes up; short phases (magma oceans after formation or a giant
// impact, snowball climates) are drawn far more often than their share of a
// planet's life, so they show up on a sheet; the composition follows where it formed (ice
// beyond the snow line, ~0.1 x Earth's insolation). A quarter are moons of a
// giant planet (cold, tidally heated, synchronous). Combinations that can't exist
// are redrawn: age past the star's life, a moon inside the host's Roche limit or
// outside its Hill sphere.
PhysicalInputs random_inputs(uint32_t seed) {
    std::mt19937 rng(seed * 2654435761u + 12345u);
    auto uni = [&](double a, double b) { return std::uniform_real_distribution<double>(a, b)(rng); };
    auto logu = [&](double a, double b) { return std::exp(uni(std::log(a), std::log(b))); };
    PhysicalInputs i;
    i.name = "random " + std::to_string(seed);
    i.star_mass = uni(0.0, 1.0) < 0.2 ? logu(0.3, 0.6) : logu(0.6, 1.4);
    const double lifetime = 10.0 * std::pow(i.star_mass, -2.5);
    // 15% caught young (1-100 Myr: magma oceans, steam atmospheres)
    i.age_gyr = uni(0.0, 1.0) < 0.15 ? logu(0.001, 0.1) : uni(0.5, std::min(10.0, 0.9 * lifetime));
    const double lum = main_sequence_lum(i.star_mass);
    const bool moon = uni(0.0, 1.0) < 0.25;
    const double pick = uni(0.0, 1.0);  // (6% of planets on scorching orbits: day-side lava)
    const double insolation = moon ? logu(0.001, 0.1) : pick < 0.4 ? logu(0.35, 1.5) : pick < 0.46 ? logu(4.0, 2000.0)
                                                      : logu(0.003, 4.0);
    i.orbit_au = std::sqrt(lum / insolation);
    const bool beyond_snow = insolation < 0.1;
    if (beyond_snow && uni(0.0, 1.0) < 0.8) {
        i.composition = Composition::IceRock;
        i.volatile_fraction = uni(0.05, 0.5);
    } else {
        i.composition = uni(0.0, 1.0) < 0.15 ? Composition::IronRich : Composition::Rocky;
        i.volatile_fraction = i.composition == Composition::IronRich ? logu(1e-7, 1e-4) : logu(1e-6, 3e-3);
    }
    i.mass = moon ? logu(0.002, 0.05) : logu(0.03, 3.0);
    i.day_hours = logu(8.0, 200.0);
    i.obliquity_deg = uni(0.0, 1.0) < 0.85 ? uni(0.0, 35.0) : uni(35.0, 180.0);
    i.moons = moon ? 0 : std::uniform_int_distribution<int>(0, 3)(rng);
    i.moon_mass_ratio = i.moons ? logu(1e-8, 0.03) : 0.0;
    // History: 10% hit by a giant impact in the last few Myr (molten again);
    // 20% start frozen over (a snowball where that climate is stable).
    if (uni(0.0, 1.0) < 0.1) i.impact_myr_ago = logu(0.05, 5.0);
    i.snowball = uni(0.0, 1.0) < 0.2;
    if (moon) {
        i.host_mass_kg = logu(0.05, 5.0) * 1.898e27;  // Neptune .. 5 Jupiters
        const double r_host = 7.0e4 * std::pow(i.host_mass_kg / 1.898e27, 0.1);  // giants are all ~Jupiter-sized
        const double hill = i.orbit_au * AU / 1e3 * std::cbrt(i.host_mass_kg / (3.0 * i.star_mass * M_SUN));
        const double lo = 2.5 * r_host, hi = 0.3 * hill;
        if (hi <= lo) return random_inputs(seed + 0x9e3779b9u);  // no stable moon orbit: redraw
        i.host_distance_km = logu(lo, std::min(hi, 40.0 * r_host));
        i.host_eccentricity = logu(1e-3, 0.03);
    }
    return i;
}

void check_presets() {
    std::printf("%-8s %13s %11s %11s %15s %11s %13s  %-32s %-34s %s\n", "body", "R km", "g", "T K", "P bar",
                "albedo", "surf age Gyr", "liquid (model | real)", "regime (model | real)", "field (model | real)");
    for (const Preset& r : presets()) {
        PlanetPhysics p = derive(r.in);
        std::printf("%-8s %6.0f|%-6.0f %5.2f|%-5.2f %5.0f|%-5.0f %7.2g|%-7.2g %5.2f|%-5.2f %6.2g|%-6.2g  %-32s %-34s %s | %s\n",
                    r.in.name.c_str(), p.radius_km, r.r_km, p.gravity, r.g, p.t_mean, r.t_k, p.pressure_bar, r.p_bar,
                    p.albedo, r.albedo, p.surface_age_gyr, r.surface_age_gyr,
                    (p.liquid + (p.ice_shell_km > 0 ? "+sub-ice ocean" : "") + " | " + r.liquid).c_str(),
                    (std::string(regime_name(p.regime)) + " | " + r.regime).c_str(), p.field.c_str(), r.field);
    }
}
