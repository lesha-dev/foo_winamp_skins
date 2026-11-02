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

    // Restricts subsequent blits to a rectangle (destination coords); reset_clip() restores the full canvas.
    void fill_rect(int x, int y, int w, int h, uint32_t bgrx);
    void set_pixel(int x, int y, uint32_t bgrx) {
        if (x >= m_clip.left && x < m_clip.right && y >= m_clip.top && y < m_clip.bottom) m_px[size_t(y) * m_w + x] = bgrx;
    }

    void set_clip(int x, int y, int w, int h) { m_clip = { x, y, x + w, y + h }; }
    void reset_clip() { m_clip = { 0, 0, m_w, m_h }; }

    // Draws to a window DC with integer scaling (nearest neighbour).
    void present(HDC dc, int scale) const;

    int width() const { return m_w; }
    int height() const { return m_h; }

private:
    int m_w = 0, m_h = 0;
    std::vector<uint32_t> m_px;
    RECT m_clip = {};
};

} // namespace wsz
