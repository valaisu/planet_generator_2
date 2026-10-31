#pragma once

#include <cstdint>
#include <string>
#include <vector>

// Write an RGBA8 buffer (row 0 = top) as PNG. scale > 1 also writes a
// nearest-neighbour upscaled copy "<stem>_x<scale>.png" for viewing pixel art.
bool write_png(const std::string& path, int w, int h, const std::vector<uint32_t>& px,
               int scale = 1);
