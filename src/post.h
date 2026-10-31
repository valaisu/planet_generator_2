#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "vec3.h"

// Palette post-process: the pixel-art look. Runs over the final RGBA8 buffer
// (after stars are composited). Per iteration: blur -> quantize each pixel to the
// nearest palette color, dithering between the two nearest colors.

enum class DitherMode { None = 0, Ordered = 1, Random = 2 };

struct PostProcess {
    bool enabled = true;
    std::vector<Vec3> palette;         // empty = quantize is a no-op
    float blur_radius = 0.0f;          // box-blur radius in px (0 = no blur)
    DitherMode dither = DitherMode::Ordered;
    int iterations = 1;                // repeat the blur+quantize pass N times
};

// Apply the pipeline in place over an RGBA8 buffer (one uint32_t per pixel, byte
// order R,G,B,A; row 0 = top). No-op when disabled or the palette is empty.
void apply_post(const PostProcess& pp, int w, int h, std::vector<uint32_t>& pixels);

// k-means (k-means++ init, deterministic from `seed`) over color samples in RGB.
// Returns k centroids sorted dark -> light.
std::vector<Vec3> kmeans_palette(const std::vector<Vec3>& samples, int k, uint32_t seed = 1);

// Palette files: one "#rrggbb" per line ('#'-only comment lines are not supported).
bool save_palette(const std::string& path, const std::vector<Vec3>& pal);
bool load_palette(const std::string& path, std::vector<Vec3>& pal);
