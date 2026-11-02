#pragma once

#include "image.h"
#include "skin_layout.h"

namespace wsz {

// Software back buffer in skin pixel space (1x). Blits clip against both source and destination,
// so undersized skin bitmaps simply leave the underlying pixels visible.
class Canvas {
public:
    void resize(int w, int h);
    void clear(uint32_t bgrx);
    void blit(const Image& src, layout::Sprite s, layout::Pos d);
    void blit(const Image& src, int sx, int sy, int w, int h, int dx, int dy);

    // Draws to a window DC with integer scaling (nearest neighbour).
    void present(HDC dc, int scale) const;

    int width() const { return m_w; }
    int height() const { return m_h; }

private:
    int m_w = 0, m_h = 0;
    std::vector<uint32_t> m_px;
};

} // namespace wsz
