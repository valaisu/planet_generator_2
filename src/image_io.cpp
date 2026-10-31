#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb/stb_image_write.h"
#define STB_IMAGE_IMPLEMENTATION
#include "stb/stb_image.h"

#include "image_io.h"

bool write_png(const std::string& path, int w, int h, const std::vector<uint32_t>& px,
               int scale) {
    if (!stbi_write_png(path.c_str(), w, h, 4, px.data(), w * 4)) return false;
    if (scale <= 1) return true;
    int sw = w * scale, sh = h * scale;
    std::vector<uint32_t> big(static_cast<size_t>(sw) * sh);
    for (int y = 0; y < sh; ++y)
        for (int x = 0; x < sw; ++x)
            big[static_cast<size_t>(y) * sw + x] = px[static_cast<size_t>(y / scale) * w + x / scale];
    std::string stem = path;
    if (stem.size() > 4 && stem.compare(stem.size() - 4, 4, ".png") == 0)
        stem.resize(stem.size() - 4);
    std::string up = stem + "_x" + std::to_string(scale) + ".png";
    return stbi_write_png(up.c_str(), sw, sh, 4, big.data(), sw * 4) != 0;
}
