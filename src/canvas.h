#pragma once

#include "image.h"
#include "skin_layout.h"

namespace wsz {

// Software back buffer in skin pixel space (1x), backed by a DIB section so GDI text can be drawn
// into it. Blits clip against both source and destination, so undersized skin bitmaps simply leave
// the underlying pixels visible.
class Canvas {
public:
    Canvas() = default;
    ~Canvas();
    Canvas(const Canvas&) = delete;
    Canvas& operator=(const Canvas&) = delete;

    void resize(int w, int h);
    void clear(uint32_t bgrx);
    void blit(const Image& src, layout::Sprite s, layout::Pos d);
    void blit(const Image& src, int sx, int sy, int w, int h, int dx, int dy);
    // Repeats a sprite to fill a rectangle (tiled frame parts).
    void tile(const Image& src, layout::Sprite s, int dx, int dy, int w, int h);
    void fill_rect(int x, int y, int w, int h, uint32_t bgrx);
    void set_pixel(int x, int y, uint32_t bgrx) {
        if (x >= m_clip.left && x < m_clip.right && y >= m_clip.top && y < m_clip.bottom) m_px[size_t(y) * m_w + x] = bgrx;
    }
    uint32_t pixel(int x, int y) const { return m_px[size_t(y) * m_w + x]; }

    // Restricts subsequent drawing to a rectangle (destination coords); reset_clip() restores the full canvas.
    void set_clip(int x, int y, int w, int h);
    void reset_clip() { set_clip(0, 0, m_w, m_h); }

    // GDI access for text. Call GdiFlush() (done by present) before reading pixels drawn through it.
    HDC dc() const { return m_dc; }

    // Draws to a window DC with integer scaling (nearest neighbour).
    void present(HDC dc, int scale) const;
    // Draws into an arbitrary rect (HALFTONE when shrinking).
    void present(HDC dc, const RECT& dst) const;

    int width() const { return m_w; }
    int height() const { return m_h; }

private:
    void release();

    int m_w = 0, m_h = 0;
    uint32_t* m_px = nullptr;
    HBITMAP m_bmp = nullptr;
    HGDIOBJ m_old = nullptr;
    HDC m_dc = nullptr;
    RECT m_clip = {};
};

uint32_t to_bgrx(COLORREF c);

} // namespace wsz
