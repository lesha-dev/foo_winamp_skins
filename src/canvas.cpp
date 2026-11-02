#include "stdafx.h"
#include "canvas.h"

namespace wsz {

void Canvas::resize(int w, int h) {
    m_w = w; m_h = h;
    m_px.assign(size_t(w) * h, 0);
    reset_clip();
}

void Canvas::clear(uint32_t bgrx) {
    std::fill(m_px.begin(), m_px.end(), bgrx);
}

void Canvas::blit(const Image& src, layout::Sprite s, layout::Pos d) {
    blit(src, s.x, s.y, s.w, s.h, d.x, d.y);
}

void Canvas::blit(const Image& src, int sx, int sy, int w, int h, int dx, int dy) {
    if (src.empty()) return;
    // Clip to source.
    if (sx < 0) { w += sx; dx -= sx; sx = 0; }
    if (sy < 0) { h += sy; dy -= sy; sy = 0; }
    w = std::min(w, src.width - sx);
    h = std::min(h, src.height - sy);
    // Clip to destination clip rect.
    if (dx < m_clip.left) { const int d = m_clip.left - dx; w -= d; sx += d; dx = m_clip.left; }
    if (dy < m_clip.top) { const int d = m_clip.top - dy; h -= d; sy += d; dy = m_clip.top; }
    w = std::min(w, static_cast<int>(m_clip.right) - dx);
    h = std::min(h, static_cast<int>(m_clip.bottom) - dy);
    if (w <= 0 || h <= 0) return;

    for (int row = 0; row < h; ++row) {
        const uint32_t* s = &src.pixels[size_t(sy + row) * src.width + sx];
        std::copy(s, s + w, &m_px[size_t(dy + row) * m_w + dx]);
    }
}

void Canvas::present(HDC dc, int scale) const {
    if (m_px.empty()) return;
    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(bmi.bmiHeader);
    bmi.bmiHeader.biWidth = m_w;
    bmi.bmiHeader.biHeight = -m_h; // top-down
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;
    SetStretchBltMode(dc, COLORONCOLOR);
    StretchDIBits(dc, 0, 0, m_w * scale, m_h * scale, 0, 0, m_w, m_h, m_px.data(), &bmi, DIB_RGB_COLORS, SRCCOPY);
}

} // namespace wsz
