// earthgen: simulate an Earth-like planet and render it in the pixel-art style.
//   earthgen [--out earth.png] [--seed N] [--grid 256] [--earth relief.f32]
//            [--maps DIR] [--size 128] [--ss 3] [--yaw DEG] [--sun-az DEG]
//            [--view disk|leo [--lat D --lon D --heading D --alt-km 400 --sun-elev 50]] [--width W --height H]
//            [--palette file.pal | --colors 18 [--plain-palette]] [--raw] [--scale 4]
//            [--frames N [--orbits 1] [--cloud-minutes 20]]   (disk animation -> <out stem>_frames/;
//            orbits 0 = fixed camera; weather time per frame, 0 = frozen clouds)
//            [--view leo --frames N [--km-per-frame 1]]      (LEO fly-along; clouds frozen)
//            [--vibrance 1.3]                                (saturation boost before the palette)
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <string>

#include "earth.h"
#include "image_io.h"
#include "maps.h"
#include "planet.h"
#include "shade.h"
#include "surface.h"
#include "view.h"

namespace {
double now_s() {
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}
}  // namespace

int main(int argc, char** argv) {
    std::string out = "earth.png", palette_path, earth_path, maps_dir, cloud_image;
    PlanetParams prm;
    int size = 128, scale = 4, ss = 3, colors = 18;
    int width = 0, height = 0;  // 0 = per-view default
    float yaw_deg = 0.0f, sun_az = 0.0f;
    int n_frames = 0;
    float orbits = 1.0f, cloud_minutes = 20.0f, km_per_frame = 1.0f, vibrance = 1.3f;
    bool raw = false, plain_palette = false, leo = false;
    LeoParams leo_prm;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&]() -> const char* {
            if (i + 1 >= argc) { std::fprintf(stderr, "missing value for %s\n", a.c_str()); std::exit(2); }
            return argv[++i];
        };
        if (a == "--out") out = next();
        else if (a == "--seed") prm.seed = static_cast<uint32_t>(std::strtoul(next(), nullptr, 10));
        else if (a == "--grid") prm.grid_n = std::atoi(next());
        else if (a == "--earth") earth_path = next();
        else if (a == "--cloud-image") cloud_image = next();
        else if (a == "--maps") maps_dir = next();
        else if (a == "--size") size = std::atoi(next());
        else if (a == "--ss") ss = std::max(1, std::atoi(next()));
        else if (a == "--yaw") yaw_deg = static_cast<float>(std::atof(next()));
        else if (a == "--sun-az") sun_az = static_cast<float>(std::atof(next()));
        else if (a == "--frames") n_frames = std::atoi(next());
        else if (a == "--km-per-frame") km_per_frame = static_cast<float>(std::atof(next()));
        else if (a == "--vibrance") vibrance = static_cast<float>(std::atof(next()));
        else if (a == "--orbits") orbits = static_cast<float>(std::atof(next()));
        else if (a == "--cloud-minutes") cloud_minutes = std::max(0.0f, static_cast<float>(std::atof(next())));
        else if (a == "--sun-elev") leo_prm.sun_elev_deg = static_cast<float>(std::atof(next()));
        else if (a == "--palette") palette_path = next();
        else if (a == "--colors") colors = std::atoi(next());
        else if (a == "--raw") raw = true;
        else if (a == "--cloud-amount") prm.cloud_amount = static_cast<float>(std::atof(next()));
        else if (a == "--view") leo = std::string(next()) == "leo";
        else if (a == "--lat") leo_prm.lat = static_cast<float>(std::atof(next()));
        else if (a == "--lon") leo_prm.lon = static_cast<float>(std::atof(next()));
        else if (a == "--heading") leo_prm.heading = static_cast<float>(std::atof(next()));
        else if (a == "--alt-km") leo_prm.alt_km = static_cast<float>(std::atof(next()));
        else if (a == "--width") width = std::atoi(next());
        else if (a == "--height") height = std::atoi(next());
        else if (a == "--plain-palette") plain_palette = true;
        else if (a == "--scale") scale = std::atoi(next());
        else { std::fprintf(stderr, "unknown arg %s\n", a.c_str()); return 2; }
    }

    // --- Simulate ---------------------------------------------------------------
    double t0 = now_s();
    Planet planet(prm.grid_n);
    if (!earth_path.empty()) {
        if (!load_earth_terrain(planet, earth_path)) {
            std::fprintf(stderr, "could not read %s\n", earth_path.c_str());
            return 1;
        }
    } else {
        generate_terrain(planet, prm);
        std::fprintf(stderr, "terrain %.2fs\n", now_s() - t0);
        erode(planet, prm);  // real Earth relief is already eroded
    }
    std::fprintf(stderr, "terrain+erosion %.2fs\n", now_s() - t0);
    simulate_climate(planet, prm);
    compute_drainage(planet);
    place_cities(planet, prm);
    std::fprintf(stderr, "climate %.2fs\n", now_s() - t0);
    CloudSimPtr weather;
    if (cloud_image.empty()) weather = start_clouds(planet, prm);
    else if (!load_cloud_image(planet, cloud_image)) {
        std::fprintf(stderr, "could not read %s\n", cloud_image.c_str());
        return 1;
    }
    std::fprintf(stderr, "clouds %.2fs\n", now_s() - t0);
    if (!maps_dir.empty()) {
        std::filesystem::create_directories(maps_dir);
        write_planet_maps(planet, maps_dir);
    }

    // --- Render -----------------------------------------------------------------
    // One frame normally; with --frames N an animation: the camera circles the
    // planet `orbits` times while the sun stays put, so day, terminator and the
    // lit night side all pass by. Every frame is rendered first, then one
    // palette is built from all of them (per-frame palettes would flicker).
    View view = leo ? leo_view(planet, leo_prm, width ? width : 256, height ? height : 128, prm.seed)
                    : disk_view(width ? width : size, yaw_deg, sun_az);
    const int W = view.width, H = view.height;
    SurfaceColors surf = surface_albedo(planet, prm.seed);
    std::vector<Vec3> relief = relief_normals(planet, view.relief);
    Scene scene{planet, surf, relief, earth_background(), prm.seed};

    struct Frame {
        std::vector<uint32_t> px;
        std::vector<Seen> seen;
    };
    auto render_frame = [&](const View& v) {
        Frame f{std::vector<uint32_t>(static_cast<size_t>(W) * H), std::vector<Seen>(static_cast<size_t>(W) * H)};
        #pragma omp parallel for schedule(dynamic, 4)
        for (int y = 0; y < H; ++y)
            for (int x = 0; x < W; ++x) {
                // ss x ss supersampling: the grid is finer than the image.
                Vec3 sum(0, 0, 0);
                for (int sy = 0; sy < ss; ++sy)
                    for (int sx = 0; sx < ss; ++sx)
                        sum = sum + shade(scene, v, pixel_ray(v.cam, x + (sx + 0.5f) / ss, y + (sy + 0.5f) / ss, W, H)).color;
                size_t i = static_cast<size_t>(y) * W + x;
                f.px[i] = pack_rgba(sum / static_cast<float>(ss * ss));
                f.seen[i] = shade(scene, v, pixel_ray(v.cam, x + 0.5f, y + 0.5f, W, H)).what;
            }
        // Grade: more saturation everywhere before the palette crush (--vibrance).
        if (!raw)
            for (uint32_t& p : f.px) {
                Vec3 c((p & 255) / 255.0f, ((p >> 8) & 255) / 255.0f, ((p >> 16) & 255) / 255.0f);
                float l = 0.299f * c.x + 0.587f * c.y + 0.114f * c.z;
                p = pack_rgba(Vec3(l, l, l) + (c - Vec3(l, l, l)) * vibrance);
            }
        return f;
    };

    std::vector<Frame> frames;
    if (n_frames <= 0) {
        frames.push_back(render_frame(view));
    } else {
        // LEO fly-along: the camera moves km_per_frame ahead each frame (the ISS
        // does ~0.3 km per 1/24 s). Clouds stay frozen: a clip is minutes of
        // real time, and animated clouds drop the fine octaves LEO needs.
        for (int f = 0; leo && f < n_frames; ++f) {
            LeoParams lp = leo_prm;
            lp.along_km = km_per_frame * f;
            frames.push_back(render_frame(leo_view(planet, lp, W, H, prm.seed)));
        }
        for (int f = 0; !leo && f < n_frames; ++f) {
            // The weather keeps running: clouds drift, grow, decay and change.
            if (weather && cloud_minutes > 0.0f) advance_clouds(*weather, planet, f == 0 ? 0.0f : cloud_minutes);
            float yaw = yaw_deg + 360.0f * orbits * f / n_frames;
            frames.push_back(render_frame(disk_view(W, yaw, sun_az - (yaw - yaw_deg))));  // sun fixed in space
        }
    }
    std::fprintf(stderr, "render %.2fs (%zu frame%s)\n", now_s() - t0, frames.size(), frames.size() > 1 ? "s" : "");

    // Palette: k-means of the image's own colours + the space colour (or a .pal
    // file). By default the budget is split by what each pixel shows, so the
    // big ocean/cloud areas can't starve land of colours —
    // space: black + 1 from the sky (glow/galactic band); of the rest, land
    // 8/16, ocean 4/16, cloud 4/16 (city lights, if any, take 3 of land's;
    // beach/cyan shallows, if any, 1 of ocean's + 1 of cloud's).
    // --plain-palette = one k-means over all. Animations sample every frame.
    PostProcess pp;
    pp.enabled = !raw;
    if (palette_path.empty()) {
        enum { SPACE, OCEAN, LAND, CLOUD, LIGHTS, COAST, NCLASS };
        std::vector<Vec3> samples[NCLASS];
        const size_t stride = frames.size() > 1 ? 7 : 1;  // keeps k-means fast on long animations
        for (const Frame& fr : frames)
            for (size_t i = 0; i < fr.px.size(); i += stride) {
                Seen w = fr.seen[i];
                int cls = plain_palette ? OCEAN
                        : w == Seen::Space ? SPACE
                        : w == Seen::Cloud ? CLOUD : w == Seen::Lights ? LIGHTS : w == Seen::Coast ? COAST
                        : w == Seen::Land ? LAND : OCEAN;
                if (plain_palette && w == Seen::Space) continue;
                uint32_t v = fr.px[i];
                samples[cls].push_back(Vec3((v & 255) / 255.0f, ((v >> 8) & 255) / 255.0f, ((v >> 16) & 255) / 255.0f));
            }
        int k[NCLASS] = {0, colors - 1, 0, 0, 0, 0};
        if (!plain_palette) {
            const int budget = colors - 2;
            k[SPACE] = 1;
            k[OCEAN] = budget / 4;
            k[CLOUD] = budget / 4;
            k[LAND] = budget - k[OCEAN] - k[CLOUD];
            if (samples[LIGHTS].size() >= 8) {  // night side with cities: 3 of land's
                k[LIGHTS] = 2;
                k[LAND] -= 3;  // the 3rd: a fixed star white (no cloud whites at night)
            }
            if (samples[COAST].size() >= 8) {  // beaches / cyan shallows: 1 of ocean's, 1 of cloud's
                k[COAST] = 2;
                k[OCEAN] -= 1;
                k[CLOUD] -= 1;
            }
        }
        pp.palette = {scene.bg.sky};
        if (k[LIGHTS] > 0) pp.palette.push_back(Vec3(0.42f, 0.43f, 0.50f));  // star grey
        for (int c = 0; c < NCLASS; ++c)
            for (Vec3 col : kmeans_palette(samples[c], k[c])) pp.palette.push_back(col);
    } else if (!load_palette(palette_path, pp.palette)) {
        std::fprintf(stderr, "could not read palette %s\n", palette_path.c_str());
        return 1;
    }

    if (frames.size() == 1) {
        apply_post(pp, W, H, frames[0].px);
        if (!write_png(out, W, H, frames[0].px, scale)) {
            std::fprintf(stderr, "could not write %s\n", out.c_str());
            return 1;
        }
    } else {
        // Frames go to <out stem>_frames/f0000.png (+ _x<scale> copies);
        // tools/animate.sh assembles them into a GIF / MP4.
        std::string dir = out.substr(0, out.rfind('.')) + "_frames";
        std::filesystem::create_directories(dir);
        for (size_t f = 0; f < frames.size(); ++f) {
            apply_post(pp, W, H, frames[f].px);
            char name[32];
            std::snprintf(name, sizeof name, "/f%04zu.png", f);
            if (!write_png(dir + name, W, H, frames[f].px, scale)) {
                std::fprintf(stderr, "could not write %s%s\n", dir.c_str(), name);
                return 1;
            }
        }
        std::fprintf(stderr, "wrote %zu frames to %s\n", frames.size(), dir.c_str());
    }
    std::fprintf(stderr, "total %.2fs\n", now_s() - t0);
    return 0;
}
