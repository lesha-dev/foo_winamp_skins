#include "stdafx.h"
#include "main_window.h"
#include "config.h"

namespace wsz {
namespace {

constexpr wchar_t window_class[] = L"foo_winamp_skins_main";
constexpr int titlebar_height = 14;

// Top-level fb2k main menu groups, shown as submenus of the context menu.
struct MenuRoot { const GUID* guid; const wchar_t* label; };
const MenuRoot menu_roots[] = {
    { &mainmenu_groups::file,     L"&File" },
    { &mainmenu_groups::edit,     L"&Edit" },
    { &mainmenu_groups::view,     L"&View" },
    { &mainmenu_groups::playback, L"&Playback" },
    { &mainmenu_groups::library,  L"&Library" },
    { &mainmenu_groups::help,     L"&Help" },
};
constexpr UINT menu_id_stride = 0x1000;

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
    wc.hIcon = ui_control::get()->get_main_icon();
    if (!RegisterClassExW(&wc)) throw exception_win32(GetLastError());
    registered = true;
}

} // namespace

HWND MainWindow::create(HookProc hook) {
    register_class(&s_wndproc);
    m_hook = hook;

    int x = static_cast<int>(config::window_x.get());
    int y = static_cast<int>(config::window_y.get());
    const int w = base_width * m_scale, h = base_height * m_scale;

    // Fall back to default placement when the stored position is off-screen.
    if (x != CW_USEDEFAULT) {
        RECT rc = { x, y, x + w, y + h };
        if (!MonitorFromRect(&rc, MONITOR_DEFAULTTONULL)) x = y = CW_USEDEFAULT;
    }

    m_wnd = CreateWindowExW(WS_EX_APPWINDOW, window_class, L"foobar2000",
        WS_POPUP | WS_SYSMENU | WS_MINIMIZEBOX | WS_CLIPCHILDREN,
        x, y, w, h, nullptr, nullptr, core_api::get_my_instance(), this);
    if (!m_wnd) throw exception_win32(GetLastError());
    return m_wnd;
}

void MainWindow::destroy() {
    if (!m_wnd) return;
    save_position();
    HWND wnd = m_wnd;
    m_wnd = nullptr;
    SetWindowLongPtrW(wnd, GWLP_USERDATA, 0);
    DestroyWindow(wnd);
}

LRESULT CALLBACK MainWindow::s_wndproc(HWND wnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        auto* self = static_cast<MainWindow*>(cs->lpCreateParams);
        self->m_wnd = wnd;
        SetWindowLongPtrW(wnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    auto* self = reinterpret_cast<MainWindow*>(GetWindowLongPtrW(wnd, GWLP_USERDATA));
    if (!self) return DefWindowProcW(wnd, msg, wp, lp);
    LRESULT ret = 0;
    if (self->m_hook && self->m_hook(wnd, msg, wp, lp, &ret)) return ret;
    return self->wndproc(msg, wp, lp);
}

LRESULT MainWindow::wndproc(UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_PAINT:
        on_paint();
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_NCHITTEST:
        return on_nchittest(lp);
    case WM_NCRBUTTONUP:
    case WM_CONTEXTMENU: {
        POINT pt = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        if (msg == WM_CONTEXTMENU && pt.x == -1 && pt.y == -1) {
            RECT rc; GetWindowRect(m_wnd, &rc);
            pt = { rc.left, rc.top };
        }
        show_context_menu(pt);
        return 0;
    }
    case WM_EXITSIZEMOVE:
        save_position();
        return 0;
    case WM_CLOSE:
        // Core runs the shutdown sequence, which calls user_interface::shutdown() -> destroy().
        standard_commands::main_exit();
        return 0;
    }
    return DefWindowProcW(m_wnd, msg, wp, lp);
}

void MainWindow::on_paint() {
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(m_wnd, &ps);
    RECT rc; GetClientRect(m_wnd, &rc);

    // Placeholder until the skin renderer lands.
    HBRUSH bg = CreateSolidBrush(RGB(32, 32, 48));
    FillRect(dc, &rc, bg);
    DeleteObject(bg);
    RECT title = { 0, 0, rc.right, titlebar_height * m_scale };
    HBRUSH tb = CreateSolidBrush(RGB(64, 64, 112));
    FillRect(dc, &title, tb);
    DeleteObject(tb);

    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(0, 230, 0));
    RECT text = rc; text.top = title.bottom;
    DrawTextW(dc, L"foo_winamp_skins\nno skin loaded\nright-click for menu", -1, &text,
        DT_CENTER | DT_VCENTER | DT_NOPREFIX);

    EndPaint(m_wnd, &ps);
}

LRESULT MainWindow::on_nchittest(LPARAM lp) {
    POINT pt = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
    ScreenToClient(m_wnd, &pt);
    if (pt.y >= 0 && pt.y < titlebar_height * m_scale) return HTCAPTION;
    return HTCLIENT;
}

void MainWindow::show_context_menu(POINT pt) {
    HMENU menu = CreatePopupMenu();
    std::vector<mainmenu_manager::ptr> managers;
    for (size_t i = 0; i < std::size(menu_roots); ++i) {
        auto mgr = mainmenu_manager::get();
        mgr->instantiate(*menu_roots[i].guid);
        HMENU sub = CreatePopupMenu();
        const UINT base = static_cast<UINT>((i + 1) * menu_id_stride);
        mgr->generate_menu_win32(sub, base, menu_id_stride - 1, mainmenu_manager::flag_show_shortcuts);
        AppendMenuW(menu, MF_POPUP | MF_STRING, reinterpret_cast<UINT_PTR>(sub), menu_roots[i].label);
        managers.push_back(std::move(mgr));
    }

    SetForegroundWindow(m_wnd);
    const UINT cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_NONOTIFY, pt.x, pt.y, 0, m_wnd, nullptr);
    DestroyMenu(menu);

    if (cmd >= menu_id_stride) {
        const size_t idx = cmd / menu_id_stride - 1;
        if (idx < managers.size()) managers[idx]->execute_command(cmd - static_cast<UINT>((idx + 1) * menu_id_stride));
    }
}

void MainWindow::save_position() {
    if (!m_wnd || IsIconic(m_wnd)) return;
    RECT rc;
    if (GetWindowRect(m_wnd, &rc)) {
        config::window_x = rc.left;
        config::window_y = rc.top;
    }
}

} // namespace wsz
