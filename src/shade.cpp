#include <algorithm>
#include <cmath>

#include "earth.h"
#include "noise.h"
#include "shade.h"

namespace {
// Cloud layer altitude: ~0.6 Rayleigh scale heights (~5 km on Earth), scaled with
// the view's atmosphere so cloud tops sit above the same share of the air (the
// exaggerated disk atmosphere otherwise tinted them cream).
float cloud_h(const View& v) { return 0.6f * 0.008f * v.atmo_scale; }

float smooth01(float a, float b, float x) {
    float t = std::clamp((x - a) / (b - a), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}
Vec3 vmul(Vec3 a, Vec3 b) { return Vec3(a.x * b.x, a.y * b.y, a.z * b.z); }

bool sphere_span(Vec3 o, Vec3 d, float rad, float& t0, float& t1) {
    float b = dot(o, d), c = dot(o, o) - rad * rad;
    float disc = b * b - c;
    if (disc < 0.0f) return false;
    float s = std::sqrt(disc);
    t0 = -b - s;
    t1 = -b + s;
    return true;
}

// Gradient (tangent, per radian) of a scalar function of direction, by forward
// differences of step h.
template <class F>
Vec3 tangent_grad(Vec3 p, float h, float f0, F&& f) {
    Vec3 e, n;
    CubeGrid::tangent_basis(p, e, n);
    return e * ((f(normalize(p + e * h)) - f0) / h) + n * ((f(normalize(p + n * h)) - f0) / h);
}

// Lambert with an ambient floor; `geo` (geometric normal) gates the night side
// so tilted relief normals can't light up past the terminator.
// `ls` blends in the Lommel-Seeliger law 2 mu0 / (mu0 + mu) (mu = cos of the view
// angle, from `to_eye`): dusty regolith scatters mostly once, so a full-phase disk
// stays flat to the limb (the full Moon), unlike a Lambert ball.
Vec3 lit(Vec3 albedo, Vec3 n, Vec3 geo, const View& v, float shadow, float ls = 0.0f, Vec3 to_eye = Vec3(0, 0, 0)) {
    float mu0 = std::max(0.0f, dot(n, v.to_sun));
    float law = mu0;
    if (ls > 0.0f) law += ls * (2.0f * mu0 / (mu0 + std::max(0.05f, dot(n, to_eye))) - mu0);
    float diffuse = law * smooth01(-0.03f, 0.08f, dot(geo, v.to_sun));
    return vmul(albedo, Vec3(v.ambient, v.ambient, v.ambient) + v.sun_color * ((1.0f - AMBIENT) * diffuse * shadow));
}
constexpr float RIVER_MIN = 8e3f;  // drainage (km^2 at 1000 mm/yr; ~250 m^3/s) for a visible river
const Vec3 LIGHT_COLOR(1.0f, 0.70f, 0.35f);  // sodium-orange city lights

// Nearest river segment to n (warped for meanders): distance and river width, km.
bool river_near(const Planet& P, Vec3 n, Vec3 off, float& dk, float& width) {
    const CubeGrid& g = P.grid;
    Vec3 w(fbm3(n * 150.0f + off * 8.0f, 2), fbm3(n * 150.0f + off * 8.0f + Vec3(3.3f, 0.0f, 1.1f), 2),
           fbm3(n * 150.0f + off * 8.0f + Vec3(0.0f, 7.7f, 2.2f), 2));
    Vec3 q = normalize(n + (w - Vec3(0.5f, 0.5f, 0.5f)) * 0.008f);
    int c0 = g.cell_of(q);
    dk = 1e9f;
    width = 0.0f;
    auto seg = [&](int c) {
        int r = P.flow_to[c];
        if (r < 0 || P.drainage[c] < RIVER_MIN) return;
        Vec3 a = g.dir(c), ab = g.dir(r) - a;
        float t = std::clamp(dot(q - a, ab) / dot(ab, ab), 0.0f, 1.0f);
        float dist = length(q - (a + ab * t)) * EARTH_RADIUS_KM;
        if (dist < dk) {
            dk = dist;
            // ~sqrt(discharge): Amazon (~6e6) ~5 km wide; fades in from RIVER_MIN.
            width = 0.002f * std::sqrt(P.drainage[c]) * smooth01(0.5f * RIVER_MIN, 2.0f * RIVER_MIN, P.drainage[c]);
        }
    };
    seg(c0);
    for (int nb : g.nbr(c0)) seg(nb);
    return dk < 1e8f;
}

// Night lights at n: each city a soft disc (flux ~ size; spread to at least half
// a pixel so small towns don't flicker in and out), plus scattered rural lights
// where habitability is high. Close up, sprawl texture breaks the discs up.
float city_light(const Planet& P, Vec3 n, Vec3 off, const View& v) {
    const CubeGrid& g = P.grid;
    if (P.city_start.empty()) return 0.0f;
    int c = g.cell_of(n);
    float L = 0.0f;
    for (int i = P.city_start[c]; i < P.city_start[c + 1]; ++i) {
        const City& ct = P.cities[P.city_index[i]];
        float r = city_radius_km(ct.size), reff = std::max(r, 0.5f * v.pixel_km);
        float dk = length(n - ct.dir) * EARTH_RADIUS_KM;
        if (dk > 2.5f * reff) continue;
        // Spreading over a coarse pixel would dim towns to nothing; let them stay
        // visible dots (up to 25x the conserved flux).
        float peak = (0.7f + 0.5f * smooth01(1.0f, 30.0f, ct.size)) * std::min(1.0f, 25.0f * (r / reff) * (r / reff));
        L += peak * std::exp(-(dk * dk) / (reff * reff));
    }
    float hab = std::clamp(g.sample(P.habitability, n) / 3.0f, 0.0f, 1.0f);
    if (v.detail) {
        L *= 0.5f + 1.2f * fbm3(n * 900.0f + off * 9.0f, 2);
        L += 0.6f * hab * hab * smooth01(0.66f, 0.74f, fbm3(n * 700.0f + off * 11.0f, 2));
    } else {
        L += 0.2f * hab * hab;
    }
    return L;
}
}  // namespace

Shaded shade(const Scene& S, const View& view, const Ray& r) {
    const Planet& P = S.planet;
    const WorldSpec& W = P.world;
    View v = view;
    v.sun_color = vmul(v.sun_color, W.star_color) * W.exposure;
    // One exposure for glow and starlight: where the lava glow outshines the
    // display, the camera exposes for it and the starlit ground, clouds and air
    // dim with it (clipping only the glow left a bright starlit rim and terminator).
    // Not under an opaque deck: it hides the glow (a dimmed deck rendered black).
    float glow_k = 1.0f;
    if (S.colors.lava_peak > 0.0f && !W.deck) glow_k = 1.0f / std::max(1.0f, S.colors.lava_peak * view.sun_color.y * W.exposure);
    v.sun_color = v.sun_color * glow_k;
    v.ambient = AMBIENT * glow_k;
    const CubeGrid& g = P.grid;
    const Vec3 off(static_cast<float>(S.seed % 101) * 1.7f, 3.1f, static_cast<float>(S.seed % 37) * 0.9f);
    Vec3 o = r.origin, d = normalize(r.direction);

    // Cloud opacity at a direction: the fitted cascade (8 octaves); with detail,
    // two more (~8 and ~4 km — finer just reads as grain at 400 km).
    // Close up, thin cloud is really unresolved cumulus: break it into small
    // opaque cells covering the same fraction (cells ~3+ px, no grain). The cells
    // are smeared along the simulated wind (line-integral convolution: cloud
    // streets, frontal bands) and domain-warped (curly, cauliflower edges).
    auto cloud_alpha = [&](Vec3 p) {
        float a = cloud_opacity(P, p, v.detail ? 10 : 8);
        if (!v.detail || a <= 0.0f) return a;
        Vec3 w = g.sample(P.cloud_wind, p);
        float ws = length(w);
        Vec3 wh = ws > 1e-4f ? w / ws : Vec3(0, 0, 0);
        float stretch = smooth01(0.1f, 0.8f, ws);
        Vec3 warp(fbm3(p * 300.0f + off, 2), fbm3(p * 300.0f + off * 1.7f + Vec3(4.1f, 0.0f, 2.3f), 2),
                  fbm3(p * 300.0f + off * 0.6f + Vec3(0.0f, 6.2f, 1.9f), 2));
        Vec3 q = p + (warp - Vec3(0.5f, 0.5f, 0.5f)) * 0.006f;
        // Both scales smeared along the wind: bands/streets (~100-300 km long)
        // of cells; the smallest cells (~2-3 px) give cumulus fields their speckle.
        float fine = 0.0f, coarse = 0.0f;
        for (int k = -2; k <= 2; ++k) {
            Vec3 qk = q + wh * (0.004f * k * stretch);
            fine += 0.6f * fbm3(qk * 500.0f + off * 3.0f, 2) + 0.4f * fbm3(qk * 1100.0f + off * 5.0f, 2);
        }
        for (int k = -1; k <= 1; ++k) coarse += fbm3((q + wh * (0.015f * k * stretch)) * 110.0f + off * 7.0f, 2);
        fine = 0.5f + (fine / 5.0f - 0.5f) * (1.0f + 1.0f * stretch);  // averaging lowers contrast
        coarse = 0.5f + (coarse / 3.0f - 0.5f) * (1.0f + 0.5f * stretch);
        float nz = 0.4f * coarse + 0.6f * fine;
        float u = std::clamp(0.5f + 3.2f * (nz - 0.5f), 0.0f, 1.0f);
        float broken = smooth01(1.0f - a - 0.05f, 1.0f - a + 0.05f, u);
        return 0.3f * a + 0.7f * broken;
    };

    float g0, g1, c0, c1;
    // A deck (Venus, Titan) tops out higher: scaled with the scale height.
    const float top = cloud_h(v) * (W.deck ? W.optics.height : 1.0f);
    bool ground = sphere_span(o, d, 1.0f, g0, g1) && g0 > 0.0f;
    bool shell = sphere_span(o, d, 1.0f + top, c0, c1) && c0 > 0.0f;

    // Ground (or stars); nothing under an opaque deck.
    Vec3 base(0, 0, 0);
    Vec3 glow(0, 0, 0);  // light the ground gives off itself (lava): no sunlight path reddening
    Seen what = Seen::Space;
    if (ground && !W.deck) {
        Vec3 n = normalize(o + d * g0);
        float h = g.sample(P.elevation, n);
        const float relief_amp = 300.0f + 0.35f * std::fabs(h);  // rougher in high terrain
        auto detail_h = [&](Vec3 p) {
            // Sub-grid relief, domain-warped so coasts get capes, bays and spits
            // rather than round noise blobs; finest octave ~10 km (finer = grain).
            Vec3 w(fbm3(p * 40.0f + off, 2), fbm3(p * 40.0f + off * 1.3f + Vec3(5.2f, 1.3f, 0.0f), 2),
                   fbm3(p * 40.0f + off * 0.7f + Vec3(0.0f, 9.1f, 2.7f), 2));
            Vec3 q = p + (w - Vec3(0.5f, 0.5f, 0.5f)) * 0.03f;
            return (2.0f * fbm3(q * 130.0f + off * 2.0f, 4) - 1.0f) * relief_amp;
        };
        float hd = v.detail ? detail_h(n) : 0.0f;
        // Detail may move the coastline only near the grid's coast: elsewhere
        // it would sprinkle seas over inland lowlands and islands over shelves.
        float near = smooth01(70.0f, 30.0f, g.sample(S.colors.coast_km, n));
        float hc = h + (h > 0.0f ? std::max(hd, -0.95f * h) : std::min(hd, -0.95f * h)) * (1.0f - near) + hd * near;
        bool land = hc > 0.0f;
        what = land ? Seen::Land : Seen::Ocean;
        Vec3 albedo = g.sample(land ? S.colors.land : S.colors.ocean, n);
        Vec3 normal = n;
        float sand = g.sample(S.colors.sand, n);
        if (!land) {
            // Turquoise on patchy tropical banks (Bahamas) and over sandy bottoms;
            // silty off big river mouths.
            float bank = v.detail ? smooth01(0.45f, 0.62f, fbm3(n * 60.0f + off * 6.0f, 3)) : 1.0f;
            Vec3 deep = albedo;
            albedo = shallow_water(albedo, hc, g.sample(S.colors.warm, n) * bank, sand,
                                   g.sample(S.colors.mud, n));
            if (length(albedo - deep) > 0.12f) what = Seen::Coast;
        }
        if (land) {
            normal = normalize(g.sample(S.relief, n));
            if (v.detail) {
                Vec3 grad = tangent_grad(n, 1.0f / 3000.0f, hd, detail_h) / (EARTH_RADIUS_KM * 1000.0f);
                normal = normalize(normal - grad * 10.0f);
                // Texture follows the terrain: hollows greener/darker (wetter),
                // rises paler/drier, plus patchy cover at ~50 km.
                float rel = hd / relief_amp;
                float patch = fbm3(n * 120.0f + off * 4.0f, 3);
                // (fbm3 with 3-4 octaves: median 0.5, sd ~0.12; rel sd ~0.22.)
                float wet = std::clamp(0.5f - 1.0f * rel + 2.5f * (patch - 0.5f), 0.0f, 1.0f);
                albedo = vmul(albedo, Vec3(1.30f, 1.18f, 0.88f) * (1.0f - wet) + Vec3(0.70f, 0.90f, 0.70f) * wet);
                // Lakes in low, flat, wet country (Florida, Canadian shield),
                // rarer elsewhere (~2% vs ~0.5% of the area); not on ice.
                float lowland = smooth01(700.0f, 100.0f, h);
                float rain = smooth01(500.0f, 1500.0f, g.sample(P.precip, n));
                float lake = fbm3(n * 160.0f + off * 5.0f, 3) - 0.08f * rel;
                bool water = lake > 0.81f - 0.05f * lowland * rain && albedo.y < 0.5f;
                // Rivers along the drainage network (each big-enough cell to the
                // cell it drains to), warped so they meander off the grid; a
                // green strip along them through dry land (the Nile).
                float dk, width;
                bool river = false;
                if (!water && river_near(P, n, off, dk, width)) {
                    // At least a pixel's ground footprint here (grows toward the
                    // horizon), so rivers stay continuous lines instead of dots.
                    float foot = g0 * EARTH_RADIUS_KM * v.pixel_rad / std::max(0.25f, -dot(d, n));
                    float draw = std::max(width, std::max(v.pixel_km, foot));
                    float dry = smooth01(700.0f, 250.0f, g.sample(P.precip, n));
                    float band = 4.0f + 3.0f * width;
                    albedo = albedo + (Vec3(0.10f, 0.22f, 0.07f) - albedo) * (dry * smooth01(band, 0.0f, dk));
                    river = water = dk < 0.5f * draw && hd < relief_amp * 0.3f;  // not over ridges
                }
                // Beaches: a pale strip on sandy shores (dunes, barrier islands).
                if (!water && near > 0.3f && hc < 25.0f * sand) {
                    albedo = albedo + (BEACH_SAND - albedo) * smooth01(0.1f, 0.5f, sand);
                    what = Seen::Coast;
                }
                if (water) {
                    // Lakes dark like Florida's; rivers lighter (they catch the sky)
                    // so a 1-px line survives the palette next to dark forest.
                    albedo = river ? Vec3(0.06f, 0.16f, 0.28f) : Vec3(0.01f, 0.03f, 0.06f);
                    normal = n;
                    what = Seen::Ocean;
                }
            }
        }
        // Cloud shadow: where the sun ray from here crosses the cloud layer.
        float s0, s1, shadow = 1.0f;
        if (sphere_span(n, v.to_sun, 1.0f + cloud_h(v), s0, s1))
            shadow = 1.0f - 0.85f * cloud_alpha(normalize(n + v.to_sun * s1));
        if (land && W.relief_light > 0.0f) {
            // Mosaic-style relief light: the relief's shading under a local sun
            // relief_sun above the horizon, relative to flat ground. From the upper
            // left of the screen, as on shaded-relief maps: lit from the right or
            // below, craters read as domes.
            Vec3 side = v.cam.up() - v.cam.right();
            side = side - n * dot(side, n) + d * 1e-3f;  // (d: nonzero at the limb)
            float el = W.relief_sun * 0.0174533f;
            Vec3 L = normalize(normalize(side) * std::cos(el) + n * std::sin(el));
            float k = 0.25f + 0.75f * std::max(0.0f, dot(normal, L)) / std::sin(el);  // soft: no black one-cell spikes
            albedo = albedo * (1.0f + W.relief_light * (k - 1.0f));
            normal = normalize(n + (normal - n) * (1.0f - W.relief_light));
        }
        base = lit(albedo, normal, n, v, shadow, W.lommel_seeliger, d * -1.0f);
        if (land && W.relief_light > 0.0f) {  // a mosaic is evenly lit: no phase darkening on the day side
            float gate = smooth01(-0.03f, 0.08f, dot(n, v.to_sun));
            Vec3 flat = vmul(albedo, Vec3(v.ambient, v.ambient, v.ambient) + v.sun_color * ((1.0f - AMBIENT) * gate * shadow));
            base = base + (flat - base) * W.relief_light;
        }
        // City lights on the night side (under the cloud layer, so clouds dim them).
        float night = smooth01(0.02f, -0.12f, dot(n, v.to_sun));
        if (land && night > 0.0f) {
            float L = city_light(P, n, off, v) * night;
            // Bright cores burn whiter (and give the palette a pale colour).
            base = base + (LIGHT_COLOR + (Vec3(1.0f, 0.95f, 0.8f) - LIGHT_COLOR) * smooth01(0.6f, 1.5f, L)) * L;
            if (L > 0.08f) what = Seen::Lights;
        }
        // Lava glows by its own heat (day and night): Planck radiance relative to
        // the starlight here (WorldSpec::thermal), on the renderer's exposure.
        if (!S.colors.lava.empty()) {
            float m = g.sample(S.colors.lava, n);
            if (m > 0.0f) {
                Vec3 e = planck_rel(g.sample(S.colors.lava_k, n)) * (m * W.thermal * view.sun_color.y * W.exposure * glow_k);
                // (interpolated cells can still exceed 1: clip keeping the colour;
                // channel clipping turned it yellow-white)
                float peak = std::max(e.x, std::max(e.y, e.z));
                if (peak > 1.0f) e = e / peak;
                glow = e;
                if (0.299f * e.x + 0.587f * e.y + 0.114f * e.z > 0.08f) what = Seen::Lights;
            }
        }
    } else if (!shell || !W.deck) {
        base = background(view.sky_turn == 0.0f ? r : Ray(r.origin, rotate_about(r.direction, Vec3(0, 1, 0), view.sky_turn)), S.bg);
    }

    // Cloud layer over it. A deck is opaque everywhere; the simulated weather
    // only shades it (thin -> deck_dark, thick -> deck_light).
    float alpha = 0.0f;
    if (shell) {
        Vec3 pc = normalize(o + d * c0);
        alpha = cloud_alpha(pc);
        if (W.deck) {
            // Broad soft swirls drawn out along latitude circles by the zonal
            // wind (noise squashed 3x north-south, warped), ~10-20 px across as
            // in the photos; the simulated weather only adds a little; seasons
            // darken one hemisphere.
            Vec3 zq(pc.x, pc.y * 3.0f, pc.z);
            Vec3 wq = zq * 2.5f + Vec3(fbm3(zq * 2.0f + off, 3), fbm3(zq * 2.0f + off * 1.3f, 3), 0.0f) * 1.5f;
            float swirl = fbm3(wq + off * 2.0f, 4);
            float shade_w = std::clamp(0.5f + W.deck_texture * (2.4f * (swirl - 0.5f) + 0.25f * (alpha - 0.5f))
                                       - W.deck_asym * pc.y, 0.0f, 1.0f);
            Vec3 col = W.deck_dark + (W.deck_light - W.deck_dark) * shade_w;
            base = lit(col, pc, pc, v, 1.0f);
            glow = Vec3(0, 0, 0);
            alpha = 1.0f;
            what = Seen::Cloud;
        } else if (alpha > 0.0f) {
            Vec3 nc = pc;
            if (v.detail) {  // lumpy cloud tops catch the low sun
                auto top = [&](Vec3 p) { return fbm3(p * 250.0f + off * 3.0f, 4); };
                nc = normalize(pc - tangent_grad(pc, 1.0f / 2000.0f, top(pc), top) * 0.0015f);
            }
            // (cloud_color is slightly blue for water clouds: the view path's
            // extinction yellows cloud tops otherwise)
            Vec3 cc = lit(W.cloud_color, nc, pc, v, 1.0f);
            glow = glow * (1.0f - alpha);
            // Over lava the cloud is lit from below: it passes on about half of
            // the glow under it, diffused (the mean molten share of the area).
            if (!P.lava_share.empty() && W.underglow > 0.0f) {
                float m = g.sample(P.lava_share, pc);
                if (m > 0.0f) {
                    Vec3 e = planck_rel(g.sample(S.colors.lava_k, pc)) * (0.5f * W.underglow * m * W.thermal * view.sun_color.y * W.exposure * glow_k);
                    float peak = std::max(e.x, std::max(e.y, e.z));
                    if (peak > 1.0f) e = e / peak;
                    glow = glow + vmul(W.cloud_color, e) * alpha;
                }
            }
            base = base * (1.0f - alpha) + cc * alpha;
            if (alpha > 0.25f) what = Seen::Cloud;  // cloud edges would eat the land colours
        }
    }

    // Atmosphere down to whatever dominates the pixel: cloud tops see less air.
    float t_surface = alpha > 0.5f ? c0 : ground ? g0 : (alpha > 0.0f ? c0 : -1.0f);
    // The disk view keeps a real-thickness atmosphere over the planet (clean
    // colours) and only draws its glow rim off the edge with a thicker shell.
    // The glow shell is exaggerated ~6x: on worlds with a taller atmosphere
    // (Titan's scale height is ~6x Earth's) cap it at twice Earth's glow.
    float scale = t_surface < 0.0f && v.rim_scale > 0.0f ? v.rim_scale * W.optics.rim * std::min(1.0f, 2.0f / W.optics.height)
                                                         : v.atmo_scale;
    Vec3 col = atmosphere(o, d, base, t_surface, v.to_sun, v.sun_color, scale,
                          alpha > 0.5f ? 1.0f : v.haze * W.optics.haze, W.optics, glow);  // cloud tops sit above the haze
    return {t_surface >= 0.0f ? col * view.gain : col, what};
}
