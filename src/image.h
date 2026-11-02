#pragma once

#include <cstdint>
#include <vector>

namespace wsz {

// 32-bit BGRX pixels, top-down, row-major (matches a top-down 32bpp DIB).
struct Image {
    int width = 0;
    int height = 0;
    std::vector<uint32_t> pixels;

    bool empty() const { return width <= 0 || height <= 0; }
};

// Decodes BMP (any variant WIC understands; also PNG/GIF masquerading as .bmp). Throws on failure.
Image decode_image(const void* data, size_t size);

} // namespace wsz
