// stylize_ref: turn a photo of Earth into a pixel-art reference in the same
// framing, starfield and post-process as earthgen, so it's a direct target.
//   stylize_ref <in.jpg> <out.png> [--size 128] [--colors 18] [--scale 4]
//               [--thresh 0.03] [--circle cx,cy,r] [--raw]
//   stylize_ref <in.jpg> <out.png> --frame [--crop x,y,w,h] [--width 256 --height 128]
// The default (disk) mode finds the planet disk and reframes it like
// `earthgen`'s disk view. --frame is for low-orbit photos (no disk): crop
// (default: largest centred 2:1 region) and box-downsample to earthgen's
// `--view leo` size; near-black space becomes the starfield.
// Writes <out>.png, <out>_x<scale>.png and the extracted palette <out>.pal.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "stb/stb_image.h"

#include "earth.h"
#include "image_io.h"

namespace {

struct Circle { float cx, cy, r; };

// Planet disk = bounding box of the largest bright connected blob (stars and
// small moons are separate, smaller blobs).
Circle find_disk(const unsigned char* img, int w, int h, float thresh) {
    std::vector<int> comp(static_cast<size_t>(w) * h, -1);
    auto bright = [&](int i) {
        const unsigned char* p = img + 3 * static_cast<size_t>(i);
        return std::max({p[0], p[1], p[2]}) > thresh * 255.0f;
    };
    int best_n = 0, bx0 = 0, by0 = 0, bx1 = w - 1, by1 = h - 1;
    std::vector<int> stack;
    for (int start = 0; start < w * h; ++start) {
        if (comp[start] >= 0 || !bright(start)) continue;
        int n = 0, x0 = w, y0 = h, x1 = 0, y1 = 0;
        comp[start] = start;
        stack.push_back(start);
        while (!stack.empty()) {
            int i = stack.back(); stack.pop_back();
            int x = i % w, y = i / w;
            ++n; x0 = std::min(x0, x); x1 = std::max(x1, x);
            y0 = std::min(y0, y); y1 = std::max(y1, y);
            const int nb[4] = {x > 0 ? i - 1 : -1, x < w - 1 ? i + 1 : -1,
                               y > 0 ? i - w : -1, y < h - 1 ? i + w : -1};
            for (int j : nb)
                if (j >= 0 && comp[j] < 0 && bright(j)) { comp[j] = start; stack.push_back(j); }
        }
        if (n > best_n) { best_n = n; bx0 = x0; by0 = y0; bx1 = x1; by1 = y1; }
    }
    // A dark night limb can shave one side off the blob; the longer bbox side is
    // the most reliable diameter.
    float r = 0.5f * std::max(bx1 - bx0 + 1, by1 - by0 + 1);
    return {0.5f * (bx0 + bx1 + 1), 0.5f * (by0 + by1 + 1), r};
}

// Low-orbit photo -> W x H pixel art. Returns the planet-pixel colour samples.
std::vector<Vec3> frame_pixels(const unsigned char* img, int w, int h, int cx, int cy, int cw, int chh,
                               int W, int H, std::vector<uint32_t>& px) {
    // Any camera works for the stars (they're hashed by direction); point it
    // along a low-orbit-like view so the density matches earthgen's sky.
    Camera cam(Vec3(0, 0, 0), Vec3(0, 0.3f, -1.0f), Vec3(0, 1, 0), 35.0f, static_cast<float>(W) / H);
    Background bg = earth_background();
    std::vector<Vec3> samples;
    px.assign(static_cast<size_t>(W) * H, 0);
    const float kx = static_cast<float>(cw) / W, ky = static_cast<float>(chh) / H;
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x) {
            int x0 = cx + static_cast<int>(x * kx), x1 = std::max(x0 + 1, cx + static_cast<int>((x + 1) * kx));
            int y0 = cy + static_cast<int>(y * ky), y1 = std::max(y0 + 1, cy + static_cast<int>((y + 1) * ky));
            Vec3 sum(0, 0, 0);
            int n = 0;
            for (int sy = y0; sy < y1; ++sy)
                for (int sx = x0; sx < x1; ++sx) {
                    const unsigned char* p = img + 3 * (static_cast<size_t>(std::clamp(sy, 0, h - 1)) * w + std::clamp(sx, 0, w - 1));
                    sum = sum + Vec3(p[0], p[1], p[2]) * (1.0f / 255.0f);
                    ++n;
                }
            Vec3 col = sum / static_cast<float>(n);
            float luma = 0.299f * col.x + 0.587f * col.y + 0.114f * col.z;
            float space = std::clamp((0.08f - luma) / 0.05f, 0.0f, 1.0f);  // black sky -> stars
            if (space < 0.5f) samples.push_back(col);
            Vec3 sky = background(pixel_ray(cam, x + 0.5f, y + 0.5f, W, H), bg);
            px[static_cast<size_t>(y) * W + x] = pack_rgba(col * (1.0f - space) + sky * space);
        }
    return samples;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: stylize_ref <in> <out.png> [--size N] [--colors K] "
                             "[--scale S] [--thresh T] [--circle cx,cy,r]\n"
                             "       stylize_ref <in> <out.png> --frame [--crop x,y,w,h] [--width W --height H]\n");
        return 2;
    }
    std::string in = argv[1], out = argv[2];
    int size = 128, colors = 18, scale = 4;
    float thresh = 0.03f;
    bool manual = false, frame = false, raw = false;
    int fw = 256, fh = 128, crop[4] = {-1, -1, -1, -1};
    Circle disk{};
    for (int i = 3; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&]() -> const char* {
            if (i + 1 >= argc) { std::fprintf(stderr, "missing value for %s\n", a.c_str()); std::exit(2); }
            return argv[++i];
        };
        if (a == "--size") size = std::atoi(next());
        else if (a == "--colors") colors = std::atoi(next());
        else if (a == "--scale") scale = std::atoi(next());
        else if (a == "--thresh") thresh = static_cast<float>(std::atof(next()));
        else if (a == "--circle") {
            manual = std::sscanf(next(), "%f,%f,%f", &disk.cx, &disk.cy, &disk.r) == 3;
            if (!manual) { std::fprintf(stderr, "--circle wants cx,cy,r\n"); return 2; }
        } else if (a == "--frame") frame = true;
        else if (a == "--raw") raw = true;  // no palette/dither (for statistics vs `earthgen --raw`)
        else if (a == "--width") fw = std::atoi(next());
        else if (a == "--height") fh = std::atoi(next());
        else if (a == "--crop") {
            if (std::sscanf(next(), "%d,%d,%d,%d", &crop[0], &crop[1], &crop[2], &crop[3]) != 4) {
                std::fprintf(stderr, "--crop wants x,y,w,h\n");
                return 2;
            }
        } else { std::fprintf(stderr, "unknown arg %s\n", a.c_str()); return 2; }
    }

    int w, h, ch;
    unsigned char* img = stbi_load(in.c_str(), &w, &h, &ch, 3);
    if (!img) { std::fprintf(stderr, "could not read %s\n", in.c_str()); return 1; }
    std::string stem = out.size() > 4 && out.compare(out.size() - 4, 4, ".png") == 0
                           ? out.substr(0, out.size() - 4) : out;
    if (frame) {
        if (crop[0] < 0) {  // largest centred region with the output aspect
            float aspect = static_cast<float>(fw) / fh;
            crop[2] = std::min(w, static_cast<int>(h * aspect));
            crop[3] = static_cast<int>(crop[2] / aspect);
            crop[0] = (w - crop[2]) / 2;
            crop[1] = (h - crop[3]) / 2;
        }
        std::printf("%s: %dx%d, crop %d,%d %dx%d\n", in.c_str(), w, h, crop[0], crop[1], crop[2], crop[3]);
        std::vector<uint32_t> fpx;
        std::vector<Vec3> samples = frame_pixels(img, w, h, crop[0], crop[1], crop[2], crop[3], fw, fh, fpx);
        stbi_image_free(img);
        PostProcess pp;
        pp.palette = kmeans_palette(samples, colors - 1);
        pp.palette.insert(pp.palette.begin(), earth_background().sky);
        if (!raw) apply_post(pp, fw, fh, fpx);
        if (!write_png(out, fw, fh, fpx, scale) || !save_palette(stem + ".pal", pp.palette)) {
            std::fprintf(stderr, "could not write %s\n", out.c_str());
            return 1;
        }
        return 0;
    }
    if (!manual) disk = find_disk(img, w, h, thresh);
    std::printf("%s: %dx%d, disk center (%.1f, %.1f) radius %.1f\n",
                in.c_str(), w, h, disk.cx, disk.cy, disk.r);

    // Output disk (matches earth_camera's silhouette) -> source disk, linearly.
    const float oc = 0.5f * size, orad = 0.5f * PLANET_FRAC * size;
    const float k = disk.r / orad;  // source px per output px

    Camera cam = earth_camera();
    Background bg = earth_background();
    std::vector<uint32_t> px(static_cast<size_t>(size) * size);
    std::vector<Vec3> samples;
    for (int y = 0; y < size; ++y)
        for (int x = 0; x < size; ++x) {
            // Box-average every source pixel in this output pixel's footprint,
            // tracking how much of it lies inside the planet disk.
            float sx0 = disk.cx + (x - oc) * k, sy0 = disk.cy + (y - oc) * k;
            int ix0 = static_cast<int>(std::floor(sx0)), iy0 = static_cast<int>(std::floor(sy0));
            int ix1 = static_cast<int>(std::ceil(sx0 + k)), iy1 = static_cast<int>(std::ceil(sy0 + k));
            Vec3 sum(0, 0, 0);
            int inside = 0, total = 0;
            for (int sy = iy0; sy < iy1; ++sy)
                for (int sx = ix0; sx < ix1; ++sx) {
                    ++total;
                    float dx = sx + 0.5f - disk.cx, dy = sy + 0.5f - disk.cy;
                    if (dx * dx + dy * dy > disk.r * disk.r) continue;
                    int cx = std::clamp(sx, 0, w - 1), cy = std::clamp(sy, 0, h - 1);
                    const unsigned char* p = img + 3 * (static_cast<size_t>(cy) * w + cx);
                    sum = sum + Vec3(p[0], p[1], p[2]) * (1.0f / 255.0f);
                    ++inside;
                }
            float cover = total ? static_cast<float>(inside) / total : 0.0f;
            Vec3 sky = background(pixel_ray(cam, x + 0.5f, y + 0.5f, size), bg);
            Vec3 col = sky;
            if (inside > 0) {
                Vec3 planet = sum / static_cast<float>(inside);
                col = planet * cover + sky * (1.0f - cover);
                if (cover > 0.5f) samples.push_back(planet);
            }
            px[static_cast<size_t>(y) * size + x] = pack_rgba(col);
        }
    stbi_image_free(img);

    // Palette: k-means over the planet's colors + the space background color.
    PostProcess pp;
    pp.palette = kmeans_palette(samples, colors - 1);
    pp.palette.insert(pp.palette.begin(), bg.sky);
    if (!raw) apply_post(pp, size, size, px);

    if (!write_png(out, size, size, px, scale) || !save_palette(stem + ".pal", pp.palette)) {
        std::fprintf(stderr, "could not write %s\n", out.c_str());
        return 1;
    }
    return 0;
}
