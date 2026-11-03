#include "stdafx.h"
#include "canvas.h"

namespace wsz {

uint32_t to_bgrx(COLORREF c) { return (GetRValue(c) << 16) | (GetGValue(c) << 8) | GetBValue(c); }

Canvas::~Canvas() { release(); }

void Canvas::release() {
    if (m_dc) {
        SelectObject(m_dc, m_old);
        DeleteDC(m_dc);
    }
    if (m_bmp) DeleteObject(m_bmp);
    m_dc = nullptr;
    m_bmp = nullptr;
    m_px = nullptr;
    m_w = m_h = 0;
}

void Canvas::resize(int w, int h) {
    if (w == m_w && h == m_h && m_px) return;
    release();
    if (w <= 0 || h <= 0) return;
    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(bmi.bmiHeader);
    bmi.bmiHeader.biWidth = w;
    bmi.bmiHeader.biHeight = -h; // top-down
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    m_bmp = CreateDIBSection(nullptr, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!m_bmp) throw std::bad_alloc();
    m_dc = CreateCompatibleDC(nullptr);
    m_old = SelectObject(m_dc, m_bmp);
    m_px = static_cast<uint32_t*>(bits);
    m_w = w;
    m_h = h;
    std::fill(m_px, m_px + size_t(w) * h, 0u);
    reset_clip();
}

void Canvas::set_clip(int x, int y, int w, int h) {
    m_clip = { std::max(0, x), std::max(0, y), std::min(m_w, x + w), std::min(m_h, y + h) };
    if (!m_dc) return;
    HRGN rgn = CreateRectRgn(m_clip.left, m_clip.top, m_clip.right, m_clip.bottom);
    SelectClipRgn(m_dc, rgn);
    DeleteObject(rgn);
}

void Canvas::clear(uint32_t bgrx) {
    if (m_px) std::fill(m_px, m_px + size_t(m_w) * m_h, bgrx);
}

void Canvas::blit(const Image& src, layout::Sprite s, layout::Pos d) {
    blit(src, s.x, s.y, s.w, s.h, d.x, d.y);
}

void Canvas::blit(const Image& src, int sx, int sy, int w, int h, int dx, int dy) {
    if (src.empty() || !m_px) return;
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

void Canvas::tile(const Image& src, layout::Sprite s, int dx, int dy, int w, int h) {
    if (s.w <= 0 || s.h <= 0) return;
    const RECT saved = m_clip;
    set_clip(std::max<int>(dx, saved.left), std::max<int>(dy, saved.top),
             std::min<int>(dx + w, saved.right) - std::max<int>(dx, saved.left),
             std::min<int>(dy + h, saved.bottom) - std::max<int>(dy, saved.top));
    for (int y = dy; y < dy + h; y += s.h)
        for (int x = dx; x < dx + w; x += s.w) blit(src, s, { x, y });
    set_clip(saved.left, saved.top, saved.right - saved.left, saved.bottom - saved.top);
}

void Canvas::fill_rect(int x, int y, int w, int h, uint32_t bgrx) {
    if (!m_px) return;
    const int x0 = std::max<int>(x, m_clip.left), y0 = std::max<int>(y, m_clip.top);
    const int x1 = std::min<int>(x + w, m_clip.right), y1 = std::min<int>(y + h, m_clip.bottom);
    if (x1 <= x0) return;
    for (int row = y0; row < y1; ++row) std::fill(&m_px[size_t(row) * m_w + x0], &m_px[size_t(row) * m_w + x1], bgrx);
}

void Canvas::present(HDC dc, int scale) const {
    if (!m_dc) return;
    GdiFlush();
    if (scale == 1) {
        BitBlt(dc, 0, 0, m_w, m_h, m_dc, 0, 0, SRCCOPY);
        return;
    }
    SetStretchBltMode(dc, COLORONCOLOR);
    StretchBlt(dc, 0, 0, m_w * scale, m_h * scale, m_dc, 0, 0, m_w, m_h, SRCCOPY);
}

void Canvas::present(HDC dc, const RECT& dst) const {
    if (!m_dc) return;
    GdiFlush();
    const int w = dst.right - dst.left, h = dst.bottom - dst.top;
    const bool shrink = w < m_w || h < m_h;
    SetStretchBltMode(dc, shrink ? HALFTONE : COLORONCOLOR);
    if (shrink) SetBrushOrgEx(dc, 0, 0, nullptr);
    StretchBlt(dc, dst.left, dst.top, w, h, m_dc, 0, 0, m_w, m_h, SRCCOPY);
}

} // namespace wsz
