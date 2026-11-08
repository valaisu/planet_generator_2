#pragma once

#include <cstdint>
#include <string>
#include <vector>

// Colour profile measured on a real photo (tools/color_transfer.py --save-profile):
// the photo's brightness distribution (CIELAB L* quantiles) and, per brightness
// band, its colours sorted along the band's main colour direction. Applied to a
// rendered disk, the render keeps only its structure (what is darker / lighter,
// redder / browner) and takes the photo's joint brightness/colour distribution,
// area shares included.
struct ColorProfile {
    int bands = 0, samples = 0;
    std::vector<float> L;            // quantiles 0..1 (uniform), L*
    std::vector<float> dir;          // per band: unit (a*, b*) direction, + = redder
    std::vector<float> a, b;         // per band: `samples` values along the sorted direction

    bool load(const std::string& path);
    // `planet[i]`: pixel i shows the planet; statistics from `inner[i]` pixels only
    // (the photo's limb is washed out), applied to every planet pixel.
    void apply(std::vector<uint32_t>& px, const std::vector<bool>& planet, const std::vector<bool>& inner) const;
};
