#include "stdafx.h"
#include "skin_window.h"
#include "config.h"
#include "docking.h"
#include "main_window.h"

namespace wsz {
namespace {

constexpr wchar_t window_class[] = L"foo_winamp_skins_child";

void register_class(WNDPROC proc) {
    static bool registered = false;
    if (registered) return;
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.style = CS_DBLCLKS;
    wc.lpfnWndProc = proc;
    wc.hInstance = core_api::get_my_instance();
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = window_class;
    if (!RegisterClassExW(&wc)) throw exception_win32(GetLastError());
    registered = true;
}

} // namespace

SkinWindow::~SkinWindow() { destroy(); }

void SkinWindow::create(HWND owner, const wchar_t* title, int x, int y) {
    register_class(&s_wndproc);
    const SIZE sz = skin_size();
    const int w = sz.cx * scale(), h = sz.cy * scale();
    if (x != CW_USEDEFAULT) {
        RECT rc = { x, y, x + w, y + h };
        if (!MonitorFromRect(&rc, MONITOR_DEFAULTTONULL)) x = y = CW_USEDEFAULT;
    }
    if (x == CW_USEDEFAULT) {
        RECT orc{};
        GetWindowRect(owner, &orc);
        x = orc.left;
        y = orc.bottom;
    }
    // Owned popup: no taskbar button, minimizes with the main window.
    m_wnd = CreateWindowExW(0, window_class, title, WS_POPUP | WS_CLIPCHILDREN, x, y, w, h, owner, nullptr,
                            core_api::get_my_instance(), this);
    if (!m_wnd) throw exception_win32(GetLastError());
    docking::add(m_wnd);
    m_tooltip.attach(m_wnd);
    apply_geometry();
}

void SkinWindow::destroy() {
    if (!m_wnd) return;
    docking::remove(m_wnd);
    HWND wnd = m_wnd;
    m_wnd = nullptr;
    m_tooltip.destroy();
    SetWindowLongPtrW(wnd, GWLP_USERDATA, 0);
    DestroyWindow(wnd);
}

void SkinWindow::set_skin(const Skin* skin) {
    m_skin = skin;
    if (!m_wnd) return;
    apply_region();
    invalidate();
}

void SkinWindow::show(bool on) {
    if (!m_wnd || on == visible()) return;
    ShowWindow(m_wnd, on ? SW_SHOWNOACTIVATE : SW_HIDE);
    on_visibility_changed(on);
}

void SkinWindow::invalidate() {
    if (m_wnd) InvalidateRect(m_wnd, nullptr, FALSE);
}

void SkinWindow::apply_geometry() {
    if (!m_wnd) return;
    const SIZE sz = skin_size();
    m_canvas.resize(sz.cx, sz.cy);
    RECT before{};
    GetWindowRect(m_wnd, &before);
    const auto docked = docking::docked_to(m_wnd);
    SetWindowPos(m_wnd, nullptr, 0, 0, sz.cx * scale(), sz.cy * scale(), SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    RECT after{};
    GetWindowRect(m_wnd, &after);
    docking::resized(m_wnd, before, after, docked);
    apply_region();
    invalidate();
}

void SkinWindow::apply_region() {
    const std::vector<RegionPolygon>* polys = nullptr;
    if (m_skin && region_name()) {
        auto& regions = m_skin->regions();
        auto it = regions.find(region_name());
        if (it != regions.end()) polys = &it->second;
    }
    if (!polys) {
        SetWindowRgn(m_wnd, nullptr, TRUE);
        return;
    }
    const int s = scale();
    std::vector<POINT> points;
    std::vector<INT> counts;
    for (auto& poly : *polys) {
        for (auto pt : poly.points) points.push_back({ pt.x * s, pt.y * s });
        counts.push_back(static_cast<INT>(poly.points.size()));
    }
    HRGN rgn = CreatePolyPolygonRgn(points.data(), counts.data(), static_cast<int>(counts.size()), WINDING);
    if (rgn && !SetWindowRgn(m_wnd, rgn, TRUE)) DeleteObject(rgn);
}

void SkinWindow::paint() {
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(m_wnd, &ps);
    const SIZE sz = skin_size();
    if (m_skin && m_canvas.width() == sz.cx && m_canvas.height() == sz.cy) {
        m_canvas.reset_clip();
        render(m_canvas);
        m_canvas.present(dc, scale());
    } else {
        RECT rc;
        GetClientRect(m_wnd, &rc);
        FillRect(dc, &rc, static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
    }
    EndPaint(m_wnd, &ps);
}

void SkinWindow::on_right_click(POINT screen_pt) {
    if (MainWindow* w = MainWindow::instance()) w->show_context_menu(screen_pt, m_wnd);
}

LRESULT CALLBACK SkinWindow::s_wndproc(HWND wnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_NCCREATE) {
        auto* self = static_cast<SkinWindow*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
        self->m_wnd = wnd;
        SetWindowLongPtrW(wnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    auto* self = reinterpret_cast<SkinWindow*>(GetWindowLongPtrW(wnd, GWLP_USERDATA));
    if (!self) return DefWindowProcW(wnd, msg, wp, lp);
    return self->wndproc(msg, wp, lp);
}

bool SkinWindow::title_lit() const { return m_active || !config::dim_titlebars.get(); }

void SkinWindow::save_window_position() {
    RECT rc;
    if (GetWindowRect(m_wnd, &rc)) on_position_saved(rc.left, rc.top);
}

LRESULT SkinWindow::wndproc(UINT msg, WPARAM wp, LPARAM lp) {
    LRESULT ret = 0;
    if (on_message(msg, wp, lp, ret)) return ret;
    switch (msg) {
    case WM_PAINT:
        paint();
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_LBUTTONDOWN:
    case WM_LBUTTONDBLCLK: {
        SetFocus(m_wnd);
        const POINT p = to_skin({ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) });
        // Everything that isn't a control drags the window, like Winamp.
        if (!holds_mouse() && (!m_skin || !is_control(p))) {
            if (msg == WM_LBUTTONDBLCLK) on_caption_dblclick();
            else docking::begin_move(m_wnd, false);
            return 0;
        }
        SetCapture(m_wnd);
        on_mouse_down(p, msg == WM_LBUTTONDBLCLK);
        return 0;
    }
    case WM_MOUSEMOVE: {
        if (docking::moving(m_wnd)) { m_tooltip.set(nullptr); docking::move_to_cursor(m_wnd); return 0; }
        const POINT p = to_skin({ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) });
        m_tooltip.set(GetCapture() == m_wnd || !m_skin ? nullptr : tooltip_at(p));
        on_mouse_move(p);
        return 0;
    }
    case WM_LBUTTONUP:
        if (docking::end_move(m_wnd)) { save_window_position(); return 0; }
        on_mouse_up(to_skin({ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) }));
        if (GetCapture() == m_wnd && !holds_mouse()) ReleaseCapture();
        return 0;
    case WM_CAPTURECHANGED:
        if (docking::end_move(m_wnd)) { save_window_position(); return 0; }
        if (reinterpret_cast<HWND>(lp) != m_wnd) on_mouse_leave_capture();
        return 0;
    case WM_MOUSEWHEEL:
        on_wheel(GET_WHEEL_DELTA_WPARAM(wp));
        return 0;
    case WM_KEYDOWN:
        if (on_key(wp)) return 0;
        if (MainWindow* w = MainWindow::instance(); w && w->handle_key(wp)) return 0;
        [[fallthrough]];
    case WM_SYSKEYDOWN:
        if (keyboard_shortcut_manager::get()->on_keydown_auto(wp)) return 0;
        break;
    case WM_CONTEXTMENU: {
        POINT pt = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        if (msg == WM_CONTEXTMENU && pt.x == -1 && pt.y == -1) {
            RECT rc; GetWindowRect(m_wnd, &rc);
            pt = { rc.left, rc.top };
        }
        on_right_click(pt);
        return 0;
    }
    case WM_ACTIVATE:
        m_active = LOWORD(wp) != WA_INACTIVE;
        invalidate();
        break;
    case WM_WINDOWPOSCHANGING:
        docking::snap(m_wnd, reinterpret_cast<WINDOWPOS*>(lp));
        break;
    case WM_EXITSIZEMOVE:
        save_window_position();
        return 0;
    case WM_CLOSE:
        show(false);
        return 0;
    }
    return DefWindowProcW(m_wnd, msg, wp, lp);
}

} // namespace wsz
