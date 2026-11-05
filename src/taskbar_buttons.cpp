#include "stdafx.h"
#include "taskbar_buttons.h"
#include "skin.h"
#include "skin_layout.h"

#include <shobjidl.h>

namespace wsz {
namespace {

struct ButtonDef {
    Control control;
    layout::Sprite sprite;
    const wchar_t* tip;
};

const ButtonDef buttons[] = {
    { Control::prev, layout::cb_prev, L"Previous Track" },
    { Control::play, layout::cb_play, L"Play" },
    { Control::pause, layout::cb_pause, L"Pause" },
    { Control::stop, layout::cb_stop, L"Stop" },
    { Control::next, layout::cb_next, L"Next Track" },
};

UINT taskbar_created_msg() {
    static const UINT msg = RegisterWindowMessageW(L"TaskbarButtonCreated");
    return msg;
}

// Sprite drawn into a size x size icon: 1:1 (or an integer multiple) centered when it fits, else shrunk
// with area averaging, keeping the aspect ratio.
HICON make_icon(const Image& src, layout::Sprite s, int size) {
    s.w = std::min(s.w, src.width - s.x);
    s.h = std::min(s.h, src.height - s.y);
    if (s.w <= 0 || s.h <= 0 || size <= 0) return nullptr;

    std::vector<uint32_t> px(size_t(size) * size, 0);
    const int scale = std::min(size / s.w, size / s.h);
    int w, h;
    if (scale >= 1) { w = s.w * scale; h = s.h * scale; }
    else if (s.w >= s.h) { w = size; h = std::max(1, s.h * size / s.w); }
    else { h = size; w = std::max(1, s.w * size / s.h); }
    const int ox = (size - w) / 2, oy = (size - h) / 2;
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            // Source area covered by this destination pixel.
            const double x0 = double(x) * s.w / w, x1 = double(x + 1) * s.w / w;
            const double y0 = double(y) * s.h / h, y1 = double(y + 1) * s.h / h;
            double r = 0, g = 0, b = 0, area = 0;
            for (int sy = int(y0); sy < int(std::ceil(y1)) && sy < s.h; ++sy) {
                const double wy = std::min(y1, sy + 1.0) - std::max(y0, double(sy));
                for (int sx = int(x0); sx < int(std::ceil(x1)) && sx < s.w; ++sx) {
                    const double wx = std::min(x1, sx + 1.0) - std::max(x0, double(sx));
                    const uint32_t c = src.pixels[size_t(s.y + sy) * src.width + s.x + sx];
                    const double a = wx * wy;
                    r += ((c >> 16) & 0xff) * a;
                    g += ((c >> 8) & 0xff) * a;
                    b += (c & 0xff) * a;
                    area += a;
                }
            }
            if (area <= 0) continue;
            const auto ch = [&](double v) { return static_cast<uint32_t>(std::clamp(v / area + 0.5, 0.0, 255.0)); };
            px[size_t(oy + y) * size + ox + x] = 0xff000000u | (ch(r) << 16) | (ch(g) << 8) | ch(b);
        }
    }

    BITMAPV5HEADER bi = {};
    bi.bV5Size = sizeof(bi);
    bi.bV5Width = size;
    bi.bV5Height = -size;
    bi.bV5Planes = 1;
    bi.bV5BitCount = 32;
    bi.bV5Compression = BI_BITFIELDS;
    bi.bV5RedMask = 0x00ff0000;
    bi.bV5GreenMask = 0x0000ff00;
    bi.bV5BlueMask = 0x000000ff;
    bi.bV5AlphaMask = 0xff000000;
    void* bits = nullptr;
    HDC dc = GetDC(nullptr);
    HBITMAP color = CreateDIBSection(dc, reinterpret_cast<BITMAPINFO*>(&bi), DIB_RGB_COLORS, &bits, nullptr, 0);
    ReleaseDC(nullptr, dc);
    if (!color) return nullptr;
    std::memcpy(bits, px.data(), px.size() * sizeof(uint32_t));
    HBITMAP mask = CreateBitmap(size, size, 1, 1, nullptr);
    ICONINFO ii = {};
    ii.fIcon = TRUE;
    ii.hbmColor = color;
    ii.hbmMask = mask;
    HICON icon = CreateIconIndirect(&ii);
    DeleteObject(color);
    DeleteObject(mask);
    return icon;
}

} // namespace

TaskbarButtons::TaskbarButtons() { taskbar_created_msg(); }

TaskbarButtons::~TaskbarButtons() { detach(); }

void TaskbarButtons::attach(HWND wnd) {
    m_wnd = wnd;
    // Let Explorer's notification through even when foobar2000 runs elevated.
    ChangeWindowMessageFilterEx(wnd, taskbar_created_msg(), MSGFLT_ALLOW, nullptr);
    ChangeWindowMessageFilterEx(wnd, WM_COMMAND, MSGFLT_ALLOW, nullptr);
}

void TaskbarButtons::detach() {
    if (m_taskbar) { m_taskbar->Release(); m_taskbar = nullptr; }
    m_added = false;
    m_wnd = nullptr;
    free_icons();
}

void TaskbarButtons::set_skin(const Skin* skin) {
    m_skin = skin;
    build_icons();
}

bool TaskbarButtons::on_message(UINT msg) {
    if (msg != taskbar_created_msg() || !m_wnd) return false;
    if (m_taskbar) { m_taskbar->Release(); m_taskbar = nullptr; }
    m_added = false;
    if (SUCCEEDED(CoCreateInstance(CLSID_TaskbarList, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&m_taskbar)))) {
        if (FAILED(m_taskbar->HrInit())) { m_taskbar->Release(); m_taskbar = nullptr; }
    }
    build_icons();
    add_buttons();
    return true;
}

Control TaskbarButtons::on_command(WPARAM wp) const {
    if (HIWORD(wp) != THBN_CLICKED) return Control::none;
    const UINT id = LOWORD(wp);
    return id < count ? buttons[id].control : Control::none;
}

void TaskbarButtons::add_buttons() {
    if (!m_taskbar || m_added) return;
    THUMBBUTTON tb[count] = {};
    for (size_t i = 0; i < count; ++i) {
        tb[i].dwMask = THB_ICON | THB_TOOLTIP | THB_FLAGS;
        tb[i].iId = static_cast<UINT>(i);
        tb[i].hIcon = m_icons[i];
        wcsncpy_s(tb[i].szTip, buttons[i].tip, _TRUNCATE);
        tb[i].dwFlags = m_skin && m_icons[i] ? THBF_ENABLED : THBF_HIDDEN;
    }
    m_added = SUCCEEDED(m_taskbar->ThumbBarAddButtons(m_wnd, count, tb));
}

void TaskbarButtons::update_buttons() {
    if (!m_taskbar || !m_added) return;
    THUMBBUTTON tb[count] = {};
    for (size_t i = 0; i < count; ++i) {
        tb[i].dwMask = THB_ICON | THB_FLAGS;
        tb[i].iId = static_cast<UINT>(i);
        tb[i].hIcon = m_icons[i];
        tb[i].dwFlags = m_skin && m_icons[i] ? THBF_ENABLED : THBF_HIDDEN;
    }
    m_taskbar->ThumbBarUpdateButtons(m_wnd, count, tb);
}

void TaskbarButtons::build_icons() {
    // The taskbar keeps its own copies, so the old icons can go once the new ones are set.
    std::array<HICON, count> old = m_icons;
    m_icons = {};
    if (m_skin) {
        const Image& img = m_skin->bitmap(SkinBitmap::cbuttons);
        const int size = GetSystemMetrics(SM_CXSMICON);
        for (size_t i = 0; i < count; ++i) m_icons[i] = make_icon(img, buttons[i].sprite, size);
    }
    update_buttons();
    for (HICON h : old) if (h) DestroyIcon(h);
}

void TaskbarButtons::free_icons() {
    for (HICON& h : m_icons) if (h) { DestroyIcon(h); h = nullptr; }
}

} // namespace wsz
