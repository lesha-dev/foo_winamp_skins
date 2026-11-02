#include "stdafx.h"
#include "main_window.h"
#include "config.h"
#include "player.h"
#include "version.h"

#include <cmath>

namespace wsz {
namespace L = layout;
namespace {

constexpr wchar_t window_class[] = L"foo_winamp_skins_main";
constexpr int titlebar_height = 14;
constexpr int snap_distance = 10;
constexpr UINT_PTR marquee_timer_id = 1;
constexpr UINT marquee_interval_ms = 220;
constexpr double balance_snap = 0.25;

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
enum : UINT { id_load_skin = 1, id_unload_skin, id_always_on_top, id_double_size, id_shade };

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

pfc::string8 format_time(double seconds) {
    const int s = std::max(0, static_cast<int>(seconds));
    return pfc::format(s / 60, ":", pfc::format_int(s % 60, 2));
}

int track_info_int(const metadb_handle_ptr& track, const char* field) {
    metadb_info_container::ptr info;
    if (!track.is_valid() || !track->get_info_ref(info)) return -1;
    const t_int64 v = info->info().info_get_int(field);
    return v > 0 ? static_cast<int>(v) : -1;
}

} // namespace

// ---------------------------------------------------------------------------------------------
// fb2k callbacks -> window refresh

class MainWindow::Callbacks : public play_callback_impl_base, public playlist_callback_single_impl_base {
public:
    explicit Callbacks(MainWindow& w)
        : play_callback_impl_base(),
          playlist_callback_single_impl_base(flag_on_playback_order_changed | flag_on_item_focus_change | flag_on_playlist_switch),
          m_w(w) {}

    void on_playback_starting(play_control::t_track_command, bool) override { m_w.invalidate(); }
    void on_playback_new_track(metadb_handle_ptr) override { m_w.on_track_changed(); }
    void on_playback_stop(play_control::t_stop_reason) override { m_w.on_playback_stopped(); }
    void on_playback_seek(double) override { m_w.invalidate(); }
    void on_playback_pause(bool) override { m_w.invalidate(); }
    void on_playback_edited(metadb_handle_ptr) override { m_w.on_track_changed(); }
    void on_playback_dynamic_info(const file_info& info) override { m_w.on_dynamic_info(info); }
    void on_playback_dynamic_info_track(const file_info&) override { m_w.on_track_changed(); }
    void on_playback_time(double) override { m_w.invalidate(); }
    void on_volume_change(float) override { m_w.invalidate(); }

    void on_playback_order_changed(t_size) override { m_w.invalidate(); }
    void on_item_focus_change(t_size, t_size) override { m_w.invalidate(); }
    void on_playlist_switch() override { m_w.invalidate(); }

private:
    MainWindow& m_w;
};

MainWindow::MainWindow() = default;
MainWindow::~MainWindow() = default;

// ---------------------------------------------------------------------------------------------
// lifetime

HWND MainWindow::create(HookProc hook) {
    register_class(&s_wndproc);
    m_hook = hook;
    m_scale = config::double_size.get() ? 2 : 1;
    m_shade = config::shade.get();
    m_always_on_top = config::always_on_top.get();
    titleformat_compiler::get()->compile_safe(m_title_script, "[%artist% - ]%title%[ '('%length%')']");

    int x = static_cast<int>(config::window_x.get());
    int y = static_cast<int>(config::window_y.get());
    const int w = base_width * m_scale, h = (m_shade ? L::shade_height : base_height) * m_scale;
    if (x != CW_USEDEFAULT) {
        RECT rc = { x, y, x + w, y + h };
        if (!MonitorFromRect(&rc, MONITOR_DEFAULTTONULL)) x = y = CW_USEDEFAULT;
    }

    m_wnd = CreateWindowExW(WS_EX_APPWINDOW | (m_always_on_top ? WS_EX_TOPMOST : 0), window_class, L"foobar2000",
        WS_POPUP | WS_SYSMENU | WS_MINIMIZEBOX | WS_CLIPCHILDREN,
        x, y, w, h, nullptr, nullptr, core_api::get_my_instance(), this);
    if (!m_wnd) throw exception_win32(GetLastError());

    const pfc::string8 path = config::skin_path.get();
    if (!path.is_empty()) load_skin(path, false);
    apply_geometry();

    m_callbacks = std::make_unique<Callbacks>(*this);
    on_track_changed();
    SetTimer(m_wnd, marquee_timer_id, marquee_interval_ms, nullptr);
    return m_wnd;
}

void MainWindow::destroy() {
    if (!m_wnd) return;
    m_callbacks.reset();
    save_position();
    HWND wnd = m_wnd;
    KillTimer(wnd, marquee_timer_id);
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
    case WM_NCLBUTTONDBLCLK: {
        POINT pt = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        ScreenToClient(m_wnd, &pt);
        if (wp == HTCAPTION && to_skin(pt).y < titlebar_height) set_shade(!m_shade);
        return 0;
    }
    case WM_LBUTTONDOWN:
    case WM_LBUTTONDBLCLK:
        on_lbutton_down(to_skin({ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) }));
        return 0;
    case WM_MOUSEMOVE:
        on_mouse_move(to_skin({ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) }));
        return 0;
    case WM_LBUTTONUP:
        on_lbutton_up(to_skin({ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) }));
        return 0;
    case WM_CAPTURECHANGED:
        if (reinterpret_cast<HWND>(lp) != m_wnd) cancel_drag();
        return 0;
    case WM_MOUSEWHEEL:
        if (GET_WHEEL_DELTA_WPARAM(wp) > 0) playback_control::get()->volume_up();
        else playback_control::get()->volume_down();
        return 0;
    case WM_KEYDOWN:
        if (on_key(wp)) return 0;
        break;
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
    case WM_TIMER:
        if (wp == marquee_timer_id && m_skin) {
            ++m_marquee_step;
            invalidate();
        }
        return 0;
    case WM_ACTIVATE:
        m_active = LOWORD(wp) != WA_INACTIVE;
        invalidate();
        break;
    case WM_WINDOWPOSCHANGING:
        snap_to_work_area(reinterpret_cast<WINDOWPOS*>(lp));
        break;
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

// ---------------------------------------------------------------------------------------------
// painting

void MainWindow::invalidate() {
    if (m_wnd) InvalidateRect(m_wnd, nullptr, FALSE);
}

void MainWindow::on_paint() {
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(m_wnd, &ps);
    if (m_skin) {
        render_main(m_canvas, *m_skin, build_state());
        m_canvas.present(dc, m_scale);
    } else {
        paint_placeholder(dc);
    }
    EndPaint(m_wnd, &ps);
}

void MainWindow::paint_placeholder(HDC dc) {
    RECT rc; GetClientRect(m_wnd, &rc);
    HBRUSH bg = CreateSolidBrush(RGB(32, 32, 48));
    FillRect(dc, &rc, bg);
    DeleteObject(bg);
    RECT title = { 0, 0, rc.right, titlebar_height * m_scale };
    HBRUSH tb = CreateSolidBrush(m_active ? RGB(64, 64, 128) : RGB(48, 48, 72));
    FillRect(dc, &title, tb);
    DeleteObject(tb);

    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(0, 230, 0));
    RECT text = m_shade ? title : rc;
    if (!m_shade) text.top = title.bottom;
    DrawTextW(dc, m_shade ? L"no skin loaded" : L"no skin loaded\nright-click → Load skin...", -1, &text,
        DT_CENTER | DT_VCENTER | DT_NOPREFIX | (m_shade ? DT_SINGLELINE : 0));
}

pfc::string8 MainWindow::current_title() {
    pfc::string8 out;
    auto pc = playback_control::get();
    if (pc->is_playing()) {
        pc->playback_format_title(nullptr, out, m_title_script, nullptr, playback_control::display_level_all);
    } else {
        metadb_handle_ptr item;
        if (playlist_manager::get()->activeplaylist_get_focus_item_handle(item)) item->format_title(nullptr, out, m_title_script, nullptr);
    }
    return out;
}

MainState MainWindow::build_state() {
    MainState st;
    st.active = m_active;
    st.shade = m_shade;
    st.always_on_top = m_always_on_top;
    st.double_size = m_scale == 2;
    st.pressed = m_pressed_inside ? m_pressed : Control::none;
    if (m_pressed == Control::volume || m_pressed == Control::balance || m_pressed == Control::posbar) st.pressed = m_pressed;

    auto pc = playback_control::get();
    if (pc->is_playing()) st.play = pc->is_paused() ? PlayState::paused : PlayState::playing;
    st.volume = m_pressed == Control::volume ? m_drag_value : player::db_to_slider(pc->get_volume());
    st.balance = m_balance;
    st.seekable = pc->playback_can_seek();

    double length = 0, pos = 0;
    metadb_handle_ptr track;
    if (st.play != PlayState::stopped && pc->get_now_playing(track)) {
        length = track->get_length();
        pos = pc->playback_get_position();
        if (length > 0) st.position = pos / length;
        st.channels = std::max(0, track_info_int(track, "channels"));
    }
    if (m_pressed == Control::posbar) st.position = m_drag_value;

    const auto order = player::order_state();
    st.shuffle = order.shuffle;
    st.repeat = order.repeat;

    // Time: hidden while stopped; blinks while paused.
    const bool blink_off = st.play == PlayState::paused && (GetTickCount64() / 1000) % 2 == 1;
    st.show_time = st.play != PlayState::stopped && !blink_off;
    st.time_negative = config::time_remaining.get() && length > 0;
    st.time_seconds = static_cast<int>(st.time_negative ? std::max(0.0, length - pos) : pos);

    st.marquee = !m_status.is_empty() ? m_status.get_ptr() : current_title().get_ptr();
    st.marquee_step = m_status.is_empty() ? m_marquee_step : 0;
    if (st.play != PlayState::stopped) {
        st.kbps = m_kbps;
        st.khz = m_khz;
    }
    return st;
}

// ---------------------------------------------------------------------------------------------
// input

POINT MainWindow::to_skin(POINT client) const {
    return { client.x / m_scale, client.y / m_scale };
}

const ControlRect* MainWindow::hit(POINT p) const {
    if (!m_skin) return nullptr;
    return m_shade ? hit_test(shade_controls, p.x, p.y) : hit_test(normal_controls, p.x, p.y);
}

LRESULT MainWindow::on_nchittest(LPARAM lp) {
    POINT pt = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
    ScreenToClient(m_wnd, &pt);
    // Controls get mouse input; everything else drags the window, like Winamp.
    return hit(to_skin(pt)) ? HTCLIENT : HTCAPTION;
}

void MainWindow::on_lbutton_down(POINT p) {
    const ControlRect* r = hit(p);
    if (!r) return;
    if (r->id == Control::posbar && !playback_control::get()->playback_can_seek()) return;
    m_pressed = r->id;
    m_pressed_inside = true;
    SetCapture(m_wnd);
    if (m_pressed == Control::volume || m_pressed == Control::balance || m_pressed == Control::posbar) update_slider(p);
    invalidate();
}

void MainWindow::on_mouse_move(POINT p) {
    if (m_pressed == Control::none) return;
    if (m_pressed == Control::volume || m_pressed == Control::balance || m_pressed == Control::posbar) {
        update_slider(p);
    } else {
        const ControlRect* r = hit(p);
        const bool inside = r && r->id == m_pressed;
        if (inside != m_pressed_inside) { m_pressed_inside = inside; invalidate(); }
    }
}

void MainWindow::on_lbutton_up(POINT p) {
    if (m_pressed == Control::none) return;
    const Control id = m_pressed;
    const bool inside = m_pressed_inside;
    if (id == Control::posbar) {
        auto pc = playback_control::get();
        metadb_handle_ptr track;
        if (pc->playback_can_seek() && pc->get_now_playing(track) && track->get_length() > 0)
            pc->playback_seek(m_drag_value * track->get_length());
    }
    m_pressed = Control::none;
    m_pressed_inside = false;
    m_status.reset();
    ReleaseCapture();
    if (inside && id != Control::volume && id != Control::balance && id != Control::posbar) activate_control(id);
    invalidate();
}

void MainWindow::cancel_drag() {
    if (m_pressed == Control::none) return;
    m_pressed = Control::none;
    m_pressed_inside = false;
    m_status.reset();
    invalidate();
}

void MainWindow::update_slider(POINT p) {
    auto ratio = [&](int x0, int w, int thumb_w) {
        return std::clamp(double(p.x - x0 - thumb_w / 2) / double(w - thumb_w), 0.0, 1.0);
    };
    switch (m_pressed) {
    case Control::volume: {
        m_drag_value = ratio(L::pos_volume.x, L::volume_bg.w, L::volume_thumb.w);
        playback_control::get()->set_volume(static_cast<float>(player::slider_to_db(m_drag_value)));
        m_status = pfc::format("Volume: ", static_cast<int>(m_drag_value * 100 + 0.5), "%");
        break;
    }
    case Control::balance: {
        double b = ratio(L::pos_balance.x, L::balance_bg.w, L::balance_thumb.w) * 2.0 - 1.0;
        if (std::abs(b) < balance_snap) b = 0;
        m_balance = m_drag_value = b;
        const int pct = static_cast<int>(std::abs(b) * 100 + 0.5);
        m_status = pct == 0 ? pfc::string8("Balance: Center") : pfc::format("Balance: ", pct, "% ", b < 0 ? "Left" : "Right");
        break;
    }
    case Control::posbar: {
        m_drag_value = m_shade ? ratio(L::pos_shade_posbar.x, L::shade_posbar_bg.w, L::shade_posbar_thumb.w)
                               : ratio(L::pos_posbar.x, L::posbar_bg.w, L::posbar_thumb.w);
        metadb_handle_ptr track;
        if (playback_control::get()->get_now_playing(track) && track->get_length() > 0) {
            const double len = track->get_length();
            m_status = pfc::format("Seek to: ", format_time(m_drag_value * len), "/", format_time(len),
                " (", static_cast<int>(m_drag_value * 100 + 0.5), "%)");
        }
        break;
    }
    default:
        return;
    }
    invalidate();
}

void MainWindow::activate_control(Control id) {
    switch (id) {
    case Control::options:
    case Control::clutter_o: {
        const ControlRect* r = m_shade ? find_control(shade_controls, id) : find_control(normal_controls, id);
        POINT pt = { 0, 0 };
        if (r) pt = { r->x * m_scale, (r->y + r->h) * m_scale };
        ClientToScreen(m_wnd, &pt);
        show_context_menu(pt);
        break;
    }
    case Control::minimize: ShowWindow(m_wnd, SW_MINIMIZE); break;
    case Control::shade: set_shade(!m_shade); break;
    case Control::close: PostMessageW(m_wnd, WM_CLOSE, 0, 0); break;
    case Control::clutter_a: set_always_on_top(!m_always_on_top); break;
    case Control::clutter_i: player::show_file_info(); break;
    case Control::clutter_d: set_double_size(m_scale == 1); break;
    case Control::clutter_v: break; // visualizer menu: not implemented yet
    case Control::prev: player::previous(); break;
    case Control::play: player::play(); break;
    case Control::pause: player::pause(); break;
    case Control::stop: player::stop(); break;
    case Control::next: player::next(); break;
    case Control::eject: player::open_files(); break;
    case Control::shuffle: player::toggle_shuffle(); break;
    case Control::repeat: player::toggle_repeat(); break;
    case Control::eq: break; // equalizer window: not in v1
    case Control::pl: break; // playlist window: not in v1
    case Control::time: config::time_remaining = !config::time_remaining.get(); break;
    default: break;
    }
    invalidate();
}

bool MainWindow::on_key(WPARAM vk) {
    const bool ctrl = GetKeyState(VK_CONTROL) < 0;
    auto pc = playback_control::get();
    if (ctrl) {
        switch (vk) {
        case 'D': set_double_size(m_scale == 1); return true;
        case 'A': set_always_on_top(!m_always_on_top); return true;
        case 'W': set_shade(!m_shade); return true;
        }
        return false;
    }
    switch (vk) {
    case 'Z': player::previous(); return true;
    case 'X': player::play(); return true;
    case 'C': player::pause(); return true;
    case 'V': player::stop(); return true;
    case 'B': player::next(); return true;
    case 'L': player::open_files(); return true;
    case VK_UP: pc->volume_up(); return true;
    case VK_DOWN: pc->volume_down(); return true;
    case VK_LEFT: pc->playback_seek_delta(-5); return true;
    case VK_RIGHT: pc->playback_seek_delta(5); return true;
    }
    return false;
}

void MainWindow::show_context_menu(POINT pt) {
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING, id_load_skin, L"Load &skin...");
    AppendMenuW(menu, MF_STRING | (m_skin ? 0 : MF_GRAYED), id_unload_skin, L"&Unload skin");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING | (m_always_on_top ? MF_CHECKED : 0), id_always_on_top, L"Always on &top\tCtrl+A");
    AppendMenuW(menu, MF_STRING | (m_scale == 2 ? MF_CHECKED : 0), id_double_size, L"&Double size\tCtrl+D");
    AppendMenuW(menu, MF_STRING | (m_shade ? MF_CHECKED : 0), id_shade, L"&Windowshade\tCtrl+W");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);

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

    switch (cmd) {
    case 0: break;
    case id_load_skin: browse_skin(); break;
    case id_unload_skin: unload_skin(); break;
    case id_always_on_top: set_always_on_top(!m_always_on_top); break;
    case id_double_size: set_double_size(m_scale == 1); break;
    case id_shade: set_shade(!m_shade); break;
    default:
        if (cmd >= menu_id_stride) {
            const size_t idx = cmd / menu_id_stride - 1;
            if (idx < managers.size()) managers[idx]->execute_command(cmd - static_cast<UINT>((idx + 1) * menu_id_stride));
        }
    }
}

// ---------------------------------------------------------------------------------------------
// playback events

void MainWindow::on_track_changed() {
    metadb_handle_ptr track;
    auto pc = playback_control::get();
    if (pc->get_now_playing(track)) {
        m_kbps = track_info_int(track, "bitrate");
        const int sr = track_info_int(track, "samplerate");
        m_khz = sr > 0 ? (sr + 500) / 1000 : -1;
        pfc::string8 title;
        pc->playback_format_title(nullptr, title, m_title_script, nullptr, playback_control::display_level_titles);
        uSetWindowText(m_wnd, pfc::format(title, " - foobar2000"));
    }
    m_marquee_step = 0;
    invalidate();
}

void MainWindow::on_playback_stopped() {
    m_kbps = m_khz = -1;
    m_marquee_step = 0;
    uSetWindowText(m_wnd, "foobar2000");
    invalidate();
}

void MainWindow::on_dynamic_info(const file_info& info) {
    const t_int64 br = info.info_get_bitrate_vbr();
    if (br > 0) m_kbps = static_cast<int>(br);
    invalidate();
}

// ---------------------------------------------------------------------------------------------
// window modes

void MainWindow::set_shade(bool on) {
    if (on == m_shade) return;
    m_shade = on;
    config::shade = on;
    cancel_drag();
    apply_geometry();
}

void MainWindow::set_double_size(bool on) {
    const int scale = on ? 2 : 1;
    if (scale == m_scale) return;
    m_scale = scale;
    config::double_size = on;
    apply_geometry();
}

void MainWindow::set_always_on_top(bool on) {
    m_always_on_top = on;
    config::always_on_top = on;
    SetWindowPos(m_wnd, on ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    invalidate();
}

void MainWindow::apply_geometry() {
    const int h = m_shade ? L::shade_height : base_height;
    m_canvas.resize(base_width, h);
    SetWindowPos(m_wnd, nullptr, 0, 0, base_width * m_scale, h * m_scale, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    apply_region();
    invalidate();
}

void MainWindow::apply_region() {
    const std::vector<RegionPolygon>* polys = nullptr;
    if (m_skin) {
        auto& regions = m_skin->regions();
        auto it = regions.find(m_shade ? "windowshade" : "normal");
        if (it != regions.end()) polys = &it->second;
    }
    if (!polys) {
        SetWindowRgn(m_wnd, nullptr, TRUE);
        return;
    }
    std::vector<POINT> points;
    std::vector<INT> counts;
    for (auto& poly : *polys) {
        for (auto pt : poly.points) points.push_back({ pt.x * m_scale, pt.y * m_scale });
        counts.push_back(static_cast<INT>(poly.points.size()));
    }
    HRGN rgn = CreatePolyPolygonRgn(points.data(), counts.data(), static_cast<int>(counts.size()), WINDING);
    if (rgn && !SetWindowRgn(m_wnd, rgn, TRUE)) DeleteObject(rgn); // on success the system owns rgn
}

void MainWindow::snap_to_work_area(WINDOWPOS* wp) const {
    if (wp->flags & SWP_NOMOVE) return;
    RECT rc;
    GetWindowRect(m_wnd, &rc);
    const int w = (wp->flags & SWP_NOSIZE) ? rc.right - rc.left : wp->cx;
    const int h = (wp->flags & SWP_NOSIZE) ? rc.bottom - rc.top : wp->cy;
    RECT target = { wp->x, wp->y, wp->x + w, wp->y + h };
    MONITORINFO mi = {};
    mi.cbSize = sizeof(mi);
    if (!GetMonitorInfoW(MonitorFromRect(&target, MONITOR_DEFAULTTONEAREST), &mi)) return;
    const RECT& wa = mi.rcWork;
    if (std::abs(target.left - wa.left) < snap_distance) wp->x = wa.left;
    else if (std::abs(target.right - wa.right) < snap_distance) wp->x = wa.right - w;
    if (std::abs(target.top - wa.top) < snap_distance) wp->y = wa.top;
    else if (std::abs(target.bottom - wa.bottom) < snap_distance) wp->y = wa.bottom - h;
}

void MainWindow::save_position() {
    if (!m_wnd || IsIconic(m_wnd)) return;
    RECT rc;
    if (GetWindowRect(m_wnd, &rc)) {
        config::window_x = rc.left;
        config::window_y = rc.top;
    }
}

// ---------------------------------------------------------------------------------------------
// skins

bool MainWindow::load_skin(const char* path, bool interactive) {
    try {
        auto skin = Skin::load(path);
        for (auto& w : skin->warnings()) FB2K_console_formatter() << COMPONENT_NAME ": " << path << ": " << w.c_str();
        m_skin = std::move(skin);
        config::skin_path = path;
        apply_region();
        invalidate();
        return true;
    } catch (const std::exception& e) {
        const pfc::string8 msg = pfc::format("Cannot load skin:\n", path, "\n\n", e.what());
        FB2K_console_formatter() << COMPONENT_NAME ": " << msg;
        if (interactive) popup_message::g_show(msg, COMPONENT_NAME, popup_message::icon_error);
        return false;
    }
}

void MainWindow::browse_skin() {
    pfc::string8 path = config::skin_path.get();
    if (uGetOpenFileName(m_wnd, "Winamp skins (*.wsz;*.zip)|*.wsz;*.zip|All files|*.*", 0, "wsz",
            "Load Winamp skin", nullptr, path, FALSE))
        load_skin(path, true);
}

void MainWindow::unload_skin() {
    m_skin.reset();
    config::skin_path = "";
    cancel_drag();
    apply_region();
    invalidate();
}

} // namespace wsz
