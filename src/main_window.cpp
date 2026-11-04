#include "stdafx.h"
#include "main_window.h"
#include "balance_dsp.h"
#include "config.h"
#include "docking.h"
#include "eq.h"
#include "guids.h"
#include "eq_window.h"
#include "playlist_window.h"
#include "element_window.h"
#include "player.h"
#include "skins_folder.h"
#include "text_font.h"
#include "version.h"

#include <cmath>

namespace wsz {
namespace L = layout;
namespace {

constexpr wchar_t window_class[] = L"foo_winamp_skins_main";
constexpr int titlebar_height = 14;
constexpr UINT_PTR marquee_timer_id = 1;
constexpr UINT marquee_interval_ms = 220;
constexpr UINT_PTR vis_timer_id = 2;
constexpr UINT vis_interval_ms = 25;

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
enum : UINT { id_always_on_top = 1, id_double_size, id_shade, id_vis_spectrum, id_vis_osc, id_vis_off,
    id_open_skins_folder, id_toggle_eq, id_toggle_pl,
    id_skin_first = 0x100, id_skin_last = 0x7FF,
    id_playlist_first = 0x800, id_playlist_last = 0xBFF,
    id_element_first = 0xC00, id_element_last = 0xFFF }; // must stay below menu_id_stride

// Element windows are stored as: u32 count, then per window: GUID, u8 visible, i32 x, y, w, h, u32 size, config bytes.
std::vector<ElementWindow::State> load_element_states() {
    std::vector<ElementWindow::State> out;
    auto blob = config::element_windows.get();
    if (blob.is_empty()) return out;
    const auto* p = static_cast<const uint8_t*>(blob->get_ptr());
    const uint8_t* end = p + blob->size();
    auto read = [&](void* dst, size_t n) {
        if (static_cast<size_t>(end - p) < n) return false;
        memcpy(dst, p, n);
        p += n;
        return true;
    };
    uint32_t count = 0;
    if (!read(&count, 4)) return out;
    for (uint32_t i = 0; i < count; ++i) {
        ElementWindow::State s;
        uint8_t vis = 0;
        int32_t v[4] = {};
        uint32_t size = 0;
        if (!read(&s.guid, sizeof(GUID)) || !read(&vis, 1) || !read(v, sizeof(v)) || !read(&size, 4)) break;
        if (static_cast<size_t>(end - p) < size) break;
        s.visible = vis != 0;
        s.x = v[0]; s.y = v[1]; s.w = v[2]; s.h = v[3];
        s.config.assign(p, p + size);
        p += size;
        out.push_back(std::move(s));
    }
    return out;
}

void save_element_states(const std::vector<ElementWindow::State>& states) {
    std::vector<uint8_t> buf;
    auto write = [&](const void* src, size_t n) { const auto* b = static_cast<const uint8_t*>(src); buf.insert(buf.end(), b, b + n); };
    const uint32_t count = static_cast<uint32_t>(states.size());
    write(&count, 4);
    for (const auto& s : states) {
        const uint8_t vis = s.visible ? 1 : 0;
        const int32_t v[4] = { s.x, s.y, s.w, s.h };
        const uint32_t size = static_cast<uint32_t>(s.config.size());
        write(&s.guid, sizeof(GUID));
        write(&vis, 1);
        write(v, sizeof(v));
        write(&size, 4);
        write(s.config.data(), s.config.size());
    }
    config::element_windows.set(buf.data(), buf.size());
}

// Default UI's Album Art Viewer, toggled by Alt+A.
GUID album_art_guid() {
    for (auto e : ui_element::enumerate()) {
        pfc::string8 name;
        e->get_name(name);
        if (strcmp(name, "Album Art Viewer") == 0) return e->get_guid();
    }
    return pfc::guid_null;
}

// Winamp's tooltip texts; "(doesn't work)" marks what isn't implemented here.
const wchar_t* tooltip_text(Control id) {
    switch (id) {
    case Control::options: return L"Main Menu";
    case Control::minimize: return L"Minimize";
    case Control::shade: return L"Toggle Windowshade Mode";
    case Control::close: return L"Close";
    case Control::clutter_o: return L"Options Menu";
    case Control::clutter_a: return L"Toggle Always-On-Top";
    case Control::clutter_i: return L"File Info";
    case Control::clutter_d: return L"Toggle Doublesize Mode";
    case Control::clutter_v: return L"Visualization";
    case Control::prev: return L"Previous Track";
    case Control::play: return L"Play";
    case Control::pause: return L"Pause";
    case Control::stop: return L"Stop";
    case Control::next: return L"Next Track";
    case Control::eject: return L"Open file(s)";
    case Control::shuffle: return L"Shuffle Modes";
    case Control::repeat: return L"Repeat Modes";
    case Control::eq: return L"Toggle Equalizer Window";
    case Control::pl: return L"Toggle Playlist Window";
    case Control::posbar: return L"Seekbar";
    case Control::volume: return L"Volume";
    case Control::balance: return L"Balance";
    case Control::time: return L"Time Display (click to toggle elapsed/remaining)";
    case Control::marquee: return L"Song Title";
    case Control::about: return L"Winamp Skins Preferences";
    default: return nullptr;
    }
}

// Menu text with its shortcut, honouring foobar2000's "Show keyboard shortcuts in menus".
std::wstring shortcut_label(const wchar_t* text, const wchar_t* keys) {
    std::wstring s = text;
    if (standard_config_objects::query_show_keyboard_shortcuts_in_menus()) { s += L'\t'; s += keys; }
    return s;
}

std::wstring menu_label(const std::wstring& s) {
    std::wstring out;
    for (wchar_t ch : s) { if (ch == L'&') out += L'&'; out += ch; }
    return out;
}

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

    void on_playback_starting(play_control::t_track_command, bool) override { m_w.update_vis_timer(); m_w.invalidate(); }
    void on_playback_new_track(metadb_handle_ptr) override { m_w.on_track_changed(); }
    void on_playback_stop(play_control::t_stop_reason) override { m_w.on_playback_stopped(); }
    void on_playback_seek(double) override { m_w.invalidate(); }
    void on_playback_pause(bool) override { m_w.update_vis_timer(); m_w.invalidate(); }
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
MainWindow::~MainWindow() {
    if (m_title_font) DeleteObject(m_title_font);
    if (m_title_font2x) DeleteObject(m_title_font2x);
}

// Winamp draws the non-bitmap title with the playlist font; the 2x one serves the double-size alternative style.
void MainWindow::update_title_fonts() {
    if (m_title_font) DeleteObject(m_title_font);
    if (m_title_font2x) DeleteObject(m_title_font2x);
    m_title_font = m_title_font2x = nullptr;
    if (!m_skin) return;
    LOGFONTW lf = {};
    lf.lfWeight = FW_NORMAL;
    lf.lfCharSet = DEFAULT_CHARSET;
    lf.lfQuality = NONANTIALIASED_QUALITY;
    wcsncpy_s(lf.lfFaceName, pfc::stringcvt::string_wide_from_utf8(m_skin->playlist_colors().font.c_str()), _TRUNCATE);
    lf.lfHeight = -10;
    m_title_font = CreateFontIndirectW(&lf);
    lf.lfHeight = -20;
    m_title_font2x = CreateFontIndirectW(&lf);
}

// ---------------------------------------------------------------------------------------------
// lifetime

MainWindow* MainWindow::s_instance = nullptr;

namespace {
class always_on_top_notify : public config_object_notify {
public:
    t_size get_watched_object_count() override { return 1; }
    GUID get_watched_object(t_size) override { return standard_config_objects::bool_ui_always_on_top; }
    void on_watched_object_changed(const service_ptr_t<config_object>&) override {
        if (MainWindow* w = MainWindow::instance()) w->sync_always_on_top();
    }
};
FB2K_SERVICE_FACTORY(always_on_top_notify);
}

HWND MainWindow::create(HookProc hook) {
    register_class(&s_wndproc);
    s_instance = this;
    m_hook = hook;
    m_scale = config::double_size.get() ? 2 : 1;
    m_shade = config::shade.get();
    m_always_on_top = config_object::g_get_data_bool_simple(standard_config_objects::bool_ui_always_on_top, false);
    m_vis.set_mode(static_cast<VisMode>(std::clamp<int>(static_cast<int>(config::vis_mode.get()), 0, 2)));
    m_vis.set_options(config::vis_options());
    update_title_formats();
    titleformat_compiler::get()->compile_safe(m_caption_script, "[%artist% - ]%title%");

    // foobar2000's "Remember window positions" decides whether saved positions are used.
    const bool remember = standard_config_objects::query_remember_window_positions();
    int x = remember ? static_cast<int>(config::window_x.get()) : CW_USEDEFAULT;
    int y = remember ? static_cast<int>(config::window_y.get()) : CW_USEDEFAULT;
    const int w = base_width * m_scale, h = (m_shade ? L::shade_height : base_height) * m_scale;
    if (x != CW_USEDEFAULT) {
        RECT rc = { x, y, x + w, y + h };
        if (!MonitorFromRect(&rc, MONITOR_DEFAULTTONULL)) x = y = CW_USEDEFAULT;
    }

    m_wnd = CreateWindowExW(WS_EX_APPWINDOW | (m_always_on_top ? WS_EX_TOPMOST : 0), window_class, L"foobar2000",
        WS_POPUP | WS_SYSMENU | WS_MINIMIZEBOX | WS_CLIPCHILDREN,
        x, y, w, h, nullptr, nullptr, core_api::get_my_instance(), this);
    if (!m_wnd) throw exception_win32(GetLastError());
    m_tooltip.attach(m_wnd);

    const pfc::string8 path = config::skin_path.get();
    if (!path.is_empty()) load_skin(path, false);
    apply_geometry();

    if (balance::get() != 0) balance::ensure_dsp();
    eq::sync();
    docking::add(m_wnd);

    // Equalizer and playlist: default below the main window, stacked.
    RECT mrc{};
    GetWindowRect(m_wnd, &mrc);
    m_eq = std::make_unique<EqWindow>();
    m_eq->set_double_size(m_scale == 2 && config::eq_double_size.get());
    if (config::eq_shade.get()) m_eq->set_shade(true);
    m_eq->create(m_wnd, L"Winamp Equalizer", remember ? static_cast<int>(config::eq_x.get()) : CW_USEDEFAULT,
        remember ? static_cast<int>(config::eq_y.get()) : CW_USEDEFAULT);
    m_eq->set_skin(m_skin.get());
    m_pl = std::make_unique<PlaylistWindow>();
    int px = remember ? static_cast<int>(config::pl_x.get()) : CW_USEDEFAULT;
    int py = remember ? static_cast<int>(config::pl_y.get()) : CW_USEDEFAULT;
    if (px == CW_USEDEFAULT) { px = mrc.left; py = mrc.bottom + EqWindow::height * m_scale; }
    m_pl->create(m_wnd, L"Winamp Playlist Editor", px, py);
    m_pl->set_skin(m_skin.get());
    m_pl->on_skin_changed();
    // Like Default UI's playlist view: the active playlist's selection is the UI selection.
    m_selection = ui_selection_manager::get()->acquire();
    m_selection->set_playlist_selection_tracking();

    if (config::eq_visible.get()) m_eq->show(true);
    if (config::pl_visible.get()) m_pl->show(true);

    // UI element windows (Elements menu), restored with their placement and element settings.
    for (auto& s : load_element_states()) {
        if (!remember) s.x = s.y = CW_USEDEFAULT;
        ElementWindow* w = create_element_window(s);
        if (s.visible) w->show(true);
    }

    m_callbacks = std::make_unique<Callbacks>(*this);
    on_track_changed();
    SetTimer(m_wnd, marquee_timer_id, marquee_interval_ms, nullptr);
    update_vis_timer();
    return m_wnd;
}

void MainWindow::destroy() {
    if (!m_wnd) return;
    m_callbacks.reset();
    save_position();
    // Keep the visibility flags: hiding during teardown would clear them.
    if (m_eq) { m_eq->save_position(); m_eq->destroy(); m_eq.reset(); }
    if (m_pl) { m_pl->save_position(); m_pl->destroy(); m_pl.reset(); }
    save_element_windows();
    for (auto& w : m_elements) { w->release_element(); w->destroy(); }
    m_elements.clear();
    m_selection.release();
    s_instance = nullptr;
    m_tooltip.destroy();
    docking::remove(m_wnd);
    HWND wnd = m_wnd;
    KillTimer(wnd, marquee_timer_id);
    KillTimer(wnd, vis_timer_id);
    m_vis_running = false;
    m_vis.release();
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
    case WM_LBUTTONDOWN:
    case WM_LBUTTONDBLCLK: {
        const POINT p = to_skin({ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) });
        // Everything that isn't a control drags the window, like Winamp; double-click on the title bar toggles shade.
        if (m_skin && !hit(p)) {
            if (msg == WM_LBUTTONDBLCLK && p.y < titlebar_height) set_shade(!m_shade);
            else docking::begin_move(m_wnd, true);
            return 0;
        }
        on_lbutton_down(p);
        return 0;
    }
    case WM_MOUSEMOVE: {
        if (docking::moving(m_wnd)) { m_tooltip.set(nullptr); docking::move_to_cursor(m_wnd); return 0; }
        const POINT p = to_skin({ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) });
        const ControlRect* r = m_skin && GetCapture() != m_wnd ? hit(p) : nullptr;
        m_tooltip.set(r ? tooltip_text(r->id) : nullptr);
        on_mouse_move(p);
        return 0;
    }
    case WM_LBUTTONUP:
        if (docking::end_move(m_wnd)) { save_positions(); return 0; }
        on_lbutton_up(to_skin({ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) }));
        return 0;
    case WM_CAPTURECHANGED:
        if (docking::end_move(m_wnd)) { save_positions(); return 0; }
        if (reinterpret_cast<HWND>(lp) != m_wnd) cancel_drag();
        return 0;
    case WM_SETCURSOR:
        if (reinterpret_cast<HWND>(wp) == m_wnd && LOWORD(lp) == HTCLIENT) {
            POINT p; GetCursorPos(&p); ScreenToClient(m_wnd, &p);
            set_skin_cursor(m_skin.get(), cursor_at(to_skin(p)));
            return TRUE;
        }
        break;
    case WM_MOUSEWHEEL:
        if (GET_WHEEL_DELTA_WPARAM(wp) > 0) playback_control::get()->volume_up();
        else playback_control::get()->volume_down();
        return 0;
    case WM_KEYDOWN:
        if (on_key(wp)) return 0;
        if (keyboard_shortcut_manager::get()->on_keydown_auto(wp)) return 0;
        break;
    case WM_SYSKEYDOWN:
        if (handle_key(wp, true)) return 0;
        if (keyboard_shortcut_manager::get()->on_keydown_auto(wp)) return 0;
        break;
    case WM_CONTEXTMENU: {
        POINT pt = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        if (msg == WM_CONTEXTMENU && pt.x == -1 && pt.y == -1) {
            RECT rc; GetWindowRect(m_wnd, &rc);
            pt = { rc.left, rc.top };
        } else {
            // Right-click on the visualizer opens its own menu, like Winamp.
            POINT client = pt;
            ScreenToClient(m_wnd, &client);
            const ControlRect* r = hit(to_skin(client));
            if (r && r->id == Control::vis) {
                show_vis_menu(pt);
                return 0;
            }
            if (r && r->id == Control::marquee) return 0;
        }
        show_context_menu(pt);
        return 0;
    }
    case WM_TIMER:
        if (wp == marquee_timer_id && m_skin) {
            // 5 px per tick like Winamp; paused while the title is dragged and for 10 ticks after.
            if (m_pressed != Control::marquee) {
                if (m_marquee_hold > 0) --m_marquee_hold;
                else m_marquee_px += font_char_w;
            }
            invalidate();
            if (m_pl && m_pl->visible() && playback_control::get()->is_paused()) m_pl->invalidate();
        } else if (wp == vis_timer_id) {
            on_vis_timer();
        }
        return 0;
    case WM_SIZE:
        update_vis_timer();
        break;
    case WM_ACTIVATE:
        m_active = LOWORD(wp) != WA_INACTIVE;
        invalidate();
        break;
    case WM_WINDOWPOSCHANGING:
        docking::snap(m_wnd, reinterpret_cast<WINDOWPOS*>(lp));
        break;
    case WM_ENTERSIZEMOVE:
        docking::begin_group_move(m_wnd);
        return 0;
    case WM_MOVE:
        docking::group_moved(m_wnd);
        break;
    case WM_EXITSIZEMOVE:
        docking::end_group_move();
        save_positions();
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
    if (m_eq && m_eq->visible() && m_eq->shade()) m_eq->invalidate(); // shows volume/balance
}

void MainWindow::update_title_formats() {
    titleformat_compiler::get()->compile_safe(m_title_script, config::format_main.get());
    if (m_pl) m_pl->update_title_formats();
}

void MainWindow::apply_display_options() {
    update_title_formats();
    if (m_eq) m_eq->set_double_size(m_scale == 2 && config::eq_double_size.get());
    if (m_pl) m_pl->apply_resize_mode();
    for (auto& w : m_elements) w->apply_resize_mode();
    invalidate_all();
}

void MainWindow::invalidate_all() {
    invalidate();
    if (m_eq) m_eq->invalidate();
    if (m_pl) m_pl->invalidate();
    for (auto& w : m_elements) w->invalidate();
}

void MainWindow::set_status(const char* text) {
    m_status = text ? text : "";
    invalidate();
}

void MainWindow::on_eq_changed() {
    if (m_eq) m_eq->invalidate();
}

void MainWindow::toggle_eq() {
    if (m_eq) m_eq->show(!m_eq->visible());
    invalidate();
}

void MainWindow::toggle_playlist() {
    if (m_pl) m_pl->show(!m_pl->visible());
    invalidate();
}

ElementWindow* MainWindow::create_element_window(ElementWindow::State s) {
    // New windows open to the right of the main window.
    if (s.x == CW_USEDEFAULT) {
        RECT mrc{};
        GetWindowRect(m_wnd, &mrc);
        s.x = mrc.right;
        s.y = mrc.top;
    }
    auto w = std::make_unique<ElementWindow>(s);
    pfc::string8 name;
    ui_element::g_get_name(name, s.guid);
    w->create(m_wnd, pfc::stringcvt::string_wide_from_utf8(name), s.x, s.y);
    w->set_skin(m_skin.get());
    m_elements.push_back(std::move(w));
    return m_elements.back().get();
}

void MainWindow::toggle_element(const GUID& guid) {
    if (guid == pfc::guid_null) return;
    for (auto& w : m_elements) {
        if (w->guid() == guid) { w->show(!w->visible()); save_element_windows(); return; }
    }
    ElementWindow::State s;
    s.guid = guid;
    create_element_window(s)->show(true);
    save_element_windows();
}

void MainWindow::save_element_windows() {
    std::vector<ElementWindow::State> states;
    for (auto& w : m_elements) states.push_back(w->state());
    save_element_states(states);
}

bool MainWindow::handle_key(WPARAM vk, bool alt) {
    if (alt) {
        if (!config::winamp_hotkeys.get()) return false;
        if (vk == 'G') { toggle_eq(); return true; }
        if (vk == 'E') { toggle_playlist(); return true; }
        if (vk == 'A') { toggle_element(album_art_guid()); return true; }
        return false;
    }
    return on_key(vk);
}

void MainWindow::on_paint() {
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(m_wnd, &ps);
    if (m_skin) {
        const MainState st = build_state();
        render_main(m_canvas, *m_skin, st);
        m_canvas.present(dc, m_scale);
        if (m_scale == 2 && !m_shade && st.title_font && config::title_font_alt.get())
            draw_title_font(dc, 2, m_title_font2x, st);
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
        auto pm = playlist_manager::get();
        const size_t focus = pm->activeplaylist_get_focus_item();
        if (focus < pm->activeplaylist_get_item_count()) pm->activeplaylist_item_format_title(focus, nullptr, out, m_title_script, nullptr, playback_control::display_level_all);
    }
    return out;
}

MainState MainWindow::build_state() {
    MainState st;
    st.active = m_active || !config::dim_titlebars.get();
    st.clutterbar = config::clutterbar.get();
    st.shade = m_shade;
    st.always_on_top = m_always_on_top;
    st.double_size = m_scale == 2;
    st.pressed = m_pressed_inside ? m_pressed : Control::none;
    if (m_pressed == Control::volume || m_pressed == Control::balance || m_pressed == Control::posbar) st.pressed = m_pressed;

    auto pc = playback_control::get();
    if (pc->is_playing()) st.play = pc->is_paused() ? PlayState::paused : PlayState::playing;
    st.volume = m_pressed == Control::volume ? m_drag_value : player::db_to_slider(pc->get_volume());
    st.balance = m_pressed == Control::balance ? m_drag_value : balance::get();
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
    st.eq_on = m_eq && m_eq->visible();
    st.pl_on = m_pl && m_pl->visible();

    // Time: hidden while stopped; blinks while paused.
    const bool blink_off = st.play == PlayState::paused && (GetTickCount64() / 1000) % 2 == 1;
    st.show_time = st.play != PlayState::stopped && !blink_off;
    st.time_negative = config::time_remaining.get() && length > 0;
    st.time_seconds = static_cast<int>(st.time_negative ? std::max(0.0, length - pos) : pos);
    if (m_pl) m_pl->set_time(st.show_time, st.time_negative, st.time_seconds);

    st.marquee = !m_status.is_empty() ? m_status.get_ptr() : current_title().get_ptr();
    st.marquee_px = m_status.is_empty() ? m_marquee_px : 0;
    if (!config::title_bitmap_font.get()) {
        st.title_font = m_title_font;
        st.title_fg = m_skin->playlist_colors().normal;
        st.title_bg = m_skin->playlist_colors().normal_bg;
    }
    if (st.play != PlayState::stopped) {
        st.kbps = m_kbps;
        st.khz = m_khz;
    }
    if (m_vis.mode() != VisMode::off && (st.play != PlayState::stopped || m_vis_running)) st.vis = &m_vis;
    return st;
}

// ---------------------------------------------------------------------------------------------
// input

// Hit areas follow Winamp's main window cursor map.
SkinCursor MainWindow::cursor_at(POINT p) const {
    if (m_shade) {
        if (in_rect(p, 254, 3, 262, 12)) return SkinCursor::ws_winbut;
        if (in_rect(p, 244, 3, 253, 12)) return SkinCursor::ws_min;
        if (in_rect(p, 228, 3, 242, 12)) return SkinCursor::ws_posbar;
        if (in_rect(p, 264, 3, 272, 12)) return SkinCursor::ws_close;
        if (in_rect(p, 5, 3, 17, 13)) return SkinCursor::ws_mainmenu;
        return SkinCursor::ws_normal;
    }
    if (in_rect(p, 107, 58, 215, 66)) return SkinCursor::volbal;
    if (in_rect(p, 18, 73, 263, 81)) return SkinCursor::posbar;
    if (in_rect(p, 254, 3, 262, 12)) return SkinCursor::winbut;
    if (in_rect(p, 244, 3, 253, 12)) return SkinCursor::min;
    if (in_rect(p, 264, 3, 272, 12)) return SkinCursor::close;
    if (in_rect(p, 5, 3, 17, 13)) return SkinCursor::mainmenu;
    if (in_rect(p, 0, 0, 275, 13)) return SkinCursor::titlebar;
    if (in_rect(p, 105, 24, 266, 35)) return SkinCursor::songname;
    return SkinCursor::normal;
}

POINT MainWindow::to_skin(POINT client) const {
    return { client.x / m_scale, client.y / m_scale };
}

const ControlRect* MainWindow::hit(POINT p) const {
    if (!m_skin) return nullptr;
    return m_shade ? hit_test(shade_controls, p.x, p.y) : hit_test(normal_controls, p.x, p.y);
}

void MainWindow::save_positions() {
    save_position();
    if (m_eq) m_eq->save_position();
    if (m_pl) m_pl->save_position();
    for (auto& w : m_elements) w->state();
}

void MainWindow::on_lbutton_down(POINT p) {
    const ControlRect* r = hit(p);
    if (!r) return;
    if (r->id == Control::posbar && !playback_control::get()->playback_can_seek()) return;
    if (r->id == Control::balance && !balance::enabled()) return;
    m_pressed = r->id;
    m_pressed_inside = true;
    SetCapture(m_wnd);
    if (m_pressed == Control::volume || m_pressed == Control::balance || m_pressed == Control::posbar) update_slider(p);
    if (m_pressed == Control::marquee) m_marquee_drag_x = p.x;
    invalidate();
}

void MainWindow::on_mouse_move(POINT p) {
    if (m_pressed == Control::none) return;
    if (m_pressed == Control::marquee) {
        // Dragging the title scrolls it by hand, like Winamp.
        m_marquee_px -= p.x - m_marquee_drag_x;
        m_marquee_drag_x = p.x;
        invalidate();
        return;
    }
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
    if (id == Control::balance && balance::get() != 0) balance::ensure_dsp();
    if (id == Control::marquee) m_marquee_hold = 10;
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
        if (std::abs(b) * 100 < static_cast<double>(config::balance_snap.get())) b = 0;
        m_drag_value = b;
        balance::set(b);
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
    case Control::clutter_v: {
        const ControlRect* r = find_control(normal_controls, id);
        POINT pt = { (r->x + r->w) * m_scale, r->y * m_scale };
        ClientToScreen(m_wnd, &pt);
        show_vis_menu(pt);
        break;
    }
    case Control::vis: set_vis_mode(static_cast<VisMode>((static_cast<int>(m_vis.mode()) + 1) % 3)); break;
    case Control::prev: player::previous(); break;
    case Control::play: player::play(); break;
    case Control::pause: player::pause(); break;
    case Control::stop:
        if (GetKeyState(VK_CONTROL) < 0) player::toggle_stop_after_current();
        else player::stop();
        break;
    case Control::next: player::next(); break;
    case Control::eject: player::open_files(); break;
    case Control::shuffle: show_order_menu(true); break;
    case Control::repeat: show_order_menu(false); break;
    case Control::eq: toggle_eq(); break;
    case Control::pl: toggle_playlist(); break;
    case Control::about: ui_control::get()->show_preferences(guids::prefs_page); break;
    case Control::time: config::time_remaining = !config::time_remaining.get(); break;
    default: break;
    }
    invalidate();
}

bool MainWindow::on_key(WPARAM vk) {
    if (!config::winamp_hotkeys.get()) return false;
    const bool ctrl = GetKeyState(VK_CONTROL) < 0;
    auto pc = playback_control::get();
    if (ctrl) {
        switch (vk) {
        case 'D': set_double_size(m_scale == 1); return true;
        case 'A': set_always_on_top(!m_always_on_top); return true;
        case 'W': set_shade(!m_shade); return true;
        case 'V': player::toggle_stop_after_current(); return true;
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

void MainWindow::show_context_menu(POINT pt, HWND owner) {
    if (!owner) owner = m_wnd;
    const bool shortcuts = standard_config_objects::query_show_keyboard_shortcuts_in_menus();
    HMENU menu = CreatePopupMenu();

    // Skins submenu: contents of the skins folder.
    const auto skins = skins_folder::list();
    const std::wstring current = pfc::stringcvt::string_wide_from_utf8(config::skin_path.get()).get_ptr();
    HMENU skins_menu = CreatePopupMenu();
    const size_t max_skins = id_skin_last - id_skin_first + 1;
    for (size_t i = 0; i < skins.size() && i < max_skins; ++i) {
        const bool sel = m_skin && _wcsicmp(skins[i].path.c_str(), current.c_str()) == 0;
        AppendMenuW(skins_menu, MF_STRING | (sel ? MF_CHECKED : 0), id_skin_first + i, menu_label(skins[i].name).c_str());
    }
    if (skins.empty()) AppendMenuW(skins_menu, MF_STRING | MF_GRAYED, 0, L"(skins folder is empty)");
    AppendMenuW(skins_menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(skins_menu, MF_STRING, id_open_skins_folder, L"&Open skins folder");
    AppendMenuW(menu, MF_POPUP | MF_STRING, reinterpret_cast<UINT_PTR>(skins_menu), L"&Skins");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING | (m_always_on_top ? MF_CHECKED : 0), id_always_on_top, shortcut_label(L"Always on &top", L"Ctrl+A").c_str());
    AppendMenuW(menu, MF_STRING | (m_scale == 2 ? MF_CHECKED : 0), id_double_size, shortcut_label(L"&Double size", L"Ctrl+D").c_str());
    AppendMenuW(menu, MF_STRING | (m_shade ? MF_CHECKED : 0), id_shade, shortcut_label(L"&Windowshade", L"Ctrl+W").c_str());
    AppendMenuW(menu, MF_STRING | (m_eq && m_eq->visible() ? MF_CHECKED : 0), id_toggle_eq, shortcut_label(L"&Equalizer", L"Alt+G").c_str());
    AppendMenuW(menu, MF_STRING | (m_pl && m_pl->visible() ? MF_CHECKED : 0), id_toggle_pl, shortcut_label(L"&Playlist editor", L"Alt+E").c_str());
    // Elements submenu: foobar2000's UI elements by group, like Default UI's "Replace UI Element" list.
    const std::vector<ElementInfo> elements = list_elements();
    HMENU elements_menu = CreatePopupMenu();
    {
        const GUID art = album_art_guid();
        HMENU group_menu = nullptr;
        pfc::string8 group;
        for (size_t i = 0; i < elements.size() && i <= id_element_last - id_element_first; ++i) {
            const ElementInfo& e = elements[i];
            if (!group_menu || e.group != group) {
                group = e.group;
                group_menu = CreatePopupMenu();
                AppendMenuW(elements_menu, MF_POPUP | MF_STRING, reinterpret_cast<UINT_PTR>(group_menu),
                    menu_label(pfc::stringcvt::string_wide_from_utf8(group).get_ptr()).c_str());
            }
            bool open = false;
            for (auto& w : m_elements) if (w->guid() == e.guid && w->visible()) open = true;
            std::wstring label = menu_label(pfc::stringcvt::string_wide_from_utf8(e.name).get_ptr());
            if (e.guid == art && shortcuts) label += L"\tAlt+A";
            AppendMenuW(group_menu, MF_STRING | (open ? MF_CHECKED : 0), id_element_first + i, label.c_str());
        }
        if (elements.empty()) AppendMenuW(elements_menu, MF_STRING | MF_GRAYED, 0, L"(no UI elements)");
    }
    AppendMenuW(menu, MF_POPUP | MF_STRING, reinterpret_cast<UINT_PTR>(elements_menu), L"E&lements");
    // Playlists submenu: fb2k's playlists; picking one makes it active (shown in the playlist editor).
    auto pm = playlist_manager::get();
    HMENU playlists = CreatePopupMenu();
    const size_t pl_count = std::min<size_t>(pm->get_playlist_count(), id_playlist_last - id_playlist_first + 1);
    const size_t pl_active = pm->get_active_playlist();
    for (size_t i = 0; i < pl_count; ++i) {
        pfc::string8 name;
        pm->playlist_get_name(i, name);
        const std::wstring label = menu_label(pfc::stringcvt::string_wide_from_utf8(name).get_ptr());
        AppendMenuW(playlists, MF_STRING | (i == pl_active ? MF_CHECKED : 0), id_playlist_first + i, label.c_str());
    }
    if (!pl_count) AppendMenuW(playlists, MF_STRING | MF_GRAYED, 0, L"(no playlists)");
    AppendMenuW(menu, MF_POPUP | MF_STRING, reinterpret_cast<UINT_PTR>(playlists), L"P&laylists");
    HMENU vis = CreatePopupMenu();
    append_vis_items(vis);
    AppendMenuW(menu, MF_POPUP | MF_STRING, reinterpret_cast<UINT_PTR>(vis), L"&Visualization");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);

    std::vector<mainmenu_manager::ptr> managers;
    for (size_t i = 0; i < std::size(menu_roots); ++i) {
        auto mgr = mainmenu_manager::get();
        mgr->instantiate(*menu_roots[i].guid);
        HMENU sub = CreatePopupMenu();
        const UINT base = static_cast<UINT>((i + 1) * menu_id_stride);
        mgr->generate_menu_win32(sub, base, menu_id_stride - 1, shortcuts ? mainmenu_manager::flag_show_shortcuts : 0);
        AppendMenuW(menu, MF_POPUP | MF_STRING, reinterpret_cast<UINT_PTR>(sub), menu_roots[i].label);
        managers.push_back(std::move(mgr));
    }

    SetForegroundWindow(owner);
    const UINT cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_NONOTIFY, pt.x, pt.y, 0, owner, nullptr);
    DestroyMenu(menu);

    switch (cmd) {
    case 0: break;
    case id_always_on_top: set_always_on_top(!m_always_on_top); break;
    case id_double_size: set_double_size(m_scale == 1); break;
    case id_shade: set_shade(!m_shade); break;
    case id_vis_spectrum: set_vis_mode(VisMode::spectrum); break;
    case id_vis_osc: set_vis_mode(VisMode::oscilloscope); break;
    case id_vis_off: set_vis_mode(VisMode::off); break;
    case id_open_skins_folder: skins_folder::open_in_explorer(); break;
    case id_toggle_eq: toggle_eq(); break;
    case id_toggle_pl: toggle_playlist(); break;
    default:
        if (cmd >= id_element_first && cmd <= id_element_last) {
            const size_t i = cmd - id_element_first;
            if (i < elements.size()) toggle_element(elements[i].guid);
        } else if (cmd >= id_playlist_first && cmd <= id_playlist_last) {
            const size_t i = cmd - id_playlist_first;
            if (i < playlist_manager::get()->get_playlist_count()) playlist_manager::get()->set_active_playlist(i);
        } else if (cmd >= id_skin_first && cmd <= id_skin_last) {
            const size_t i = cmd - id_skin_first;
            if (i < skins.size()) load_skin(pfc::stringcvt::string_utf8_from_wide(skins[i].path.c_str()), true);
        } else if (cmd >= menu_id_stride) {
            const size_t idx = cmd / menu_id_stride - 1;
            if (idx < managers.size()) managers[idx]->execute_command(cmd - static_cast<UINT>((idx + 1) * menu_id_stride));
        }
    }
}

void MainWindow::append_vis_items(HMENU menu) {
    const VisMode m = m_vis.mode();
    AppendMenuW(menu, MF_STRING | (m == VisMode::spectrum ? MF_CHECKED : 0), id_vis_spectrum, L"&Spectrum analyzer");
    AppendMenuW(menu, MF_STRING | (m == VisMode::oscilloscope ? MF_CHECKED : 0), id_vis_osc, L"&Oscilloscope");
    AppendMenuW(menu, MF_STRING | (m == VisMode::off ? MF_CHECKED : 0), id_vis_off, L"O&ff");
}

void MainWindow::set_vis_options(const VisOptions& o) {
    config::set_vis_options(o);
    m_vis.set_options(o);
    invalidate();
}

void MainWindow::show_order_menu(bool shuffle) {
    // Menu of fb2k's matching playback orders, opened under the button.
    const auto names = player::orders(shuffle);
    const pfc::string8 active = player::active_order();
    const bool any = shuffle ? player::order_state().shuffle : player::order_state().repeat;
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING | (any ? 0 : MF_CHECKED), 1, L"None");
    for (size_t i = 0; i < names.size(); ++i) {
        const bool sel = strcmp(names[i], active) == 0;
        AppendMenuW(menu, MF_STRING | (sel ? MF_CHECKED : 0), static_cast<UINT_PTR>(i + 2), pfc::stringcvt::string_wide_from_utf8(names[i]).get_ptr());
    }
    const L::Pos pos = shuffle ? L::pos_shuffle : L::pos_repeat;
    const int h = shuffle ? L::shuffle_btn.h : L::repeat_btn.h;
    POINT pt = { pos.x * m_scale, (pos.y + h) * m_scale };
    ClientToScreen(m_wnd, &pt);
    SetForegroundWindow(m_wnd);
    const UINT cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_LEFTBUTTON | TPM_NONOTIFY, pt.x, pt.y, 0, m_wnd, nullptr);
    DestroyMenu(menu);
    if (cmd == 1) {
        if (any) player::set_order("Default"); // only clears this button's kind of order
    } else if (cmd >= 2 && cmd - 2 < names.size()) {
        player::set_order(names[cmd - 2]);
    }
    invalidate();
}

void MainWindow::show_vis_menu(POINT pt) {
    HMENU menu = CreatePopupMenu();
    append_vis_items(menu);
    SetForegroundWindow(m_wnd);
    const UINT cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_NONOTIFY, pt.x, pt.y, 0, m_wnd, nullptr);
    DestroyMenu(menu);
    switch (cmd) {
    case id_vis_spectrum: set_vis_mode(VisMode::spectrum); break;
    case id_vis_osc: set_vis_mode(VisMode::oscilloscope); break;
    case id_vis_off: set_vis_mode(VisMode::off); break;
    default: break;
    }
}

// ---------------------------------------------------------------------------------------------
// visualizer

void MainWindow::set_vis_mode(VisMode mode) {
    m_vis.set_mode(mode);
    config::vis_mode = static_cast<int>(mode);
    if (mode == VisMode::off && m_vis_running) {
        KillTimer(m_wnd, vis_timer_id);
        m_vis_running = false;
    }
    update_vis_timer();
    invalidate();
}

void MainWindow::update_vis_timer() {
    if (!m_wnd) return;
    auto pc = playback_control::get();
    const bool playing = pc->is_playing() && !pc->is_paused();
    if (IsIconic(m_wnd)) {
        if (m_vis_running) KillTimer(m_wnd, vis_timer_id);
        m_vis_running = false;
        m_vis.reset();
        return;
    }
    // Stopping is left to on_vis_timer() so bars can fall off after pause/stop.
    if (playing && m_vis.mode() != VisMode::off && !m_vis_running) {
        SetTimer(m_wnd, vis_timer_id, vis_interval_ms, nullptr);
        m_vis_running = true;
    }
}

void MainWindow::on_vis_timer() {
    auto pc = playback_control::get();
    if (pc->is_playing() && pc->is_paused()) {
        // Winamp freezes the visualizer while paused; resume continues from the frozen state.
        KillTimer(m_wnd, vis_timer_id);
        m_vis_running = false;
        m_vis.freeze();
        return;
    }
    if (!m_vis.tick(pc->is_playing() && !pc->is_paused())) {
        KillTimer(m_wnd, vis_timer_id);
        m_vis_running = false;
        m_vis.reset();
    }
    if (m_vis.take_redraw()) invalidate();
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
        pc->playback_format_title(nullptr, title, m_caption_script, nullptr, playback_control::display_level_titles);
        uSetWindowText(m_wnd, pfc::format(title, " - foobar2000"));
    }
    m_marquee_px = 0;
    m_marquee_hold = 0;
    invalidate();
}

void MainWindow::on_playback_stopped() {
    m_kbps = m_khz = -1;
    m_marquee_px = 0;
    m_marquee_hold = 0;
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
    if (m_eq) m_eq->set_double_size(on && config::eq_double_size.get());
}

void MainWindow::set_always_on_top(bool on) {
    // foobar2000's own View > Always on top; the change comes back through sync_always_on_top.
    config_object::g_set_data_bool(standard_config_objects::bool_ui_always_on_top, on);
}

void MainWindow::sync_always_on_top() {
    const bool on = config_object::g_get_data_bool_simple(standard_config_objects::bool_ui_always_on_top, false);
    if (!m_wnd || on == m_always_on_top) return;
    m_always_on_top = on;
    SetWindowPos(m_wnd, on ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    invalidate();
}

void MainWindow::apply_geometry() {
    const int h = m_shade ? L::shade_height : base_height;
    m_canvas.resize(base_width, h);
    RECT before{};
    GetWindowRect(m_wnd, &before);
    const auto docked = docking::docked_to(m_wnd);
    SetWindowPos(m_wnd, nullptr, 0, 0, base_width * m_scale, h * m_scale, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    RECT after{};
    GetWindowRect(m_wnd, &after);
    docking::resized(m_wnd, before, after, docked);
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
        if (m_eq) m_eq->set_skin(m_skin.get());
        if (m_pl) { m_pl->set_skin(m_skin.get()); m_pl->on_skin_changed(); }
        for (auto& w : m_elements) { w->set_skin(m_skin.get()); w->on_skin_changed(); }
        update_title_fonts();
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

} // namespace wsz
