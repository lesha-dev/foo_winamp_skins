#include "stdafx.h"
#include "playlist_window.h"
#include "config.h"
#include "main_window.h"
#include "player.h"
#include "text_font.h"

#include <shellapi.h>
#include <cmath>

namespace wsz {
namespace {

using S = layout::Sprite;

// pledit.bmp frame
constexpr S top_tile        { 127, 21, 25, 20 };
constexpr S top_left        { 0, 21, 25, 20 };
constexpr S top_title       { 26, 21, 100, 20 };
constexpr S top_right       { 153, 21, 25, 20 };
constexpr S top_tile_sel    { 127, 0, 25, 20 };
constexpr S top_left_sel    { 0, 0, 25, 20 };
constexpr S top_title_sel   { 26, 0, 100, 20 };
constexpr S top_right_sel   { 153, 0, 25, 20 };
constexpr S left_tile       { 0, 42, 12, 29 };
constexpr S right_tile      { 31, 42, 20, 29 };
constexpr S bottom_tile     { 179, 0, 25, 38 };
constexpr S bottom_left     { 0, 72, 125, 38 };
constexpr S bottom_right    { 126, 72, 150, 38 };
constexpr S scroll_handle   { 52, 53, 8, 18 };
constexpr S scroll_handle_down { 61, 53, 8, 18 };
constexpr S close_down      { 52, 42, 9, 9 };
constexpr S collapse_down   { 62, 42, 9, 9 };
constexpr S expand_down     { 150, 42, 9, 9 };
// windowshade
constexpr S shade_left      { 72, 42, 25, 14 };
constexpr S shade_tile      { 72, 57, 25, 14 };
constexpr S shade_right     { 99, 57, 50, 14 };
constexpr S shade_right_sel { 99, 42, 50, 14 };

constexpr int top_h = 20, bottom_h = 38, left_w = 12, right_w = 20, list_top = 2; // rows start at y=22 and end at h-38

// Sprite menus: items top to bottom.
struct MenuItem { S normal, hover; };
struct MenuDef { int x_from_left; bool from_right; S bar; std::vector<MenuItem> items; };
const MenuDef& menu_def(int idx) {
    static const MenuDef defs[] = {
        { 14, false, { 48, 111, 3, 54 }, { { { 0, 111, 22, 18 }, { 23, 111, 22, 18 } }, { { 0, 130, 22, 18 }, { 23, 130, 22, 18 } }, { { 0, 149, 22, 18 }, { 23, 149, 22, 18 } } } },
        { 43, false, { 100, 111, 3, 72 }, { { { 54, 168, 22, 18 }, { 77, 168, 22, 18 } }, { { 54, 111, 22, 18 }, { 77, 111, 22, 18 } }, { { 54, 130, 22, 18 }, { 77, 130, 22, 18 } }, { { 54, 149, 22, 18 }, { 77, 149, 22, 18 } } } },
        { 72, false, { 150, 111, 3, 54 }, { { { 104, 111, 22, 18 }, { 127, 111, 22, 18 } }, { { 104, 130, 22, 18 }, { 127, 130, 22, 18 } }, { { 104, 149, 22, 18 }, { 127, 149, 22, 18 } } } },
        { 101, false, { 200, 111, 3, 54 }, { { { 154, 111, 22, 18 }, { 177, 111, 22, 18 } }, { { 154, 130, 22, 18 }, { 177, 130, 22, 18 } }, { { 154, 149, 22, 18 }, { 177, 149, 22, 18 } } } },
        { 44, true, { 250, 111, 3, 54 }, { { { 204, 111, 22, 18 }, { 227, 111, 22, 18 } }, { { 204, 130, 22, 18 }, { 227, 130, 22, 18 } }, { { 204, 149, 22, 18 }, { 227, 149, 22, 18 } } } },
    };
    return defs[idx];
}

// Whole seconds, truncated like Winamp's track lengths.
pfc::string8 time_str(double seconds, bool hours) {
    const long s = std::max(0L, static_cast<long>(seconds));
    if (hours && s >= 3600) return pfc::format(s / 3600, ":", pfc::format_int((s / 60) % 60, 2), ":", pfc::format_int(s % 60, 2));
    return pfc::format(s / 60, ":", pfc::format_int(s % 60, 2));
}

std::wstring wide(const char* s) { return pfc::stringcvt::string_wide_from_utf8(s).get_ptr(); }

} // namespace

// ---------------------------------------------------------------------------------------------

class PlaylistWindow::Callbacks : public playlist_callback_single_impl_base, public play_callback_impl_base {
public:
    explicit Callbacks(PlaylistWindow& w) : playlist_callback_single_impl_base(static_cast<t_uint32>(flag_all)), m_w(w) {}

    void on_items_added(t_size, metadb_handle_list_cref, const bit_array&) override { m_w.playlist_changed(); }
    void on_items_reordered(const t_size*, t_size) override { m_w.playlist_changed(); }
    void on_items_removed(const bit_array&, t_size, t_size) override { m_w.playlist_changed(); }
    void on_items_selection_change(const bit_array&, const bit_array&) override { m_w.m_lengths_dirty = true; m_w.invalidate(); }
    void on_item_focus_change(t_size, t_size to) override {
        if (to != SIZE_MAX) m_w.ensure_visible(to);
        m_w.invalidate();
    }
    void on_items_modified(const bit_array&) override { m_w.playlist_changed(); }
    void on_items_replaced(const bit_array&, const pfc::list_base_const_t<playlist_callback::t_on_items_replaced_entry>&) override { m_w.playlist_changed(); }
    void on_playlist_switch() override { m_w.m_scroll = 0; m_w.m_anchor = SIZE_MAX; m_w.playlist_changed(); }
    void on_playlist_renamed(const char*, t_size) override {}

    void on_playback_new_track(metadb_handle_ptr) override { m_w.invalidate(); }
    void on_playback_stop(play_control::t_stop_reason) override { m_w.invalidate(); }
    void on_playback_pause(bool) override { m_w.invalidate(); }
    void on_playback_time(double) override { m_w.invalidate(); }

private:
    PlaylistWindow& m_w;
};

PlaylistWindow::PlaylistWindow() {
    m_shade = config::pl_shade.get();
    m_w_extra = std::clamp(config::pl_width.get() >= 0 ? static_cast<int>(config::pl_width.get()) : static_cast<int>(config::pl_w.get()) * step_w, 0, max_extra_w);
    m_h_extra = std::clamp(config::pl_height.get() >= 0 ? static_cast<int>(config::pl_height.get()) : static_cast<int>(config::pl_h.get()) * step_h, 0, max_extra_h);
    if (config::pl_use_view.get()) set_use_view(true);
    if (!config::smooth_resize.get()) { m_w_extra -= m_w_extra % step_w; m_h_extra -= m_h_extra % step_h; }
    update_title_formats();
}

void PlaylistWindow::update_title_formats() {
    titleformat_compiler::get()->compile_safe(m_title_script, config::format_playlist.get());
    titleformat_compiler::get()->compile_safe(m_shade_script, config::format_playlist_shade.get());
    invalidate();
}

PlaylistWindow::~PlaylistWindow() {
    release_element();
    m_callbacks.reset();
    destroy();
    if (m_font) DeleteObject(m_font);
    if (m_shade_font) DeleteObject(m_shade_font);
}

int PlaylistWindow::width() const { return min_width + m_w_extra; }
int PlaylistWindow::height() const { return m_shade ? shade_height : min_height + m_h_extra; }
SIZE PlaylistWindow::skin_size() const { return { width(), height() }; }

PlaylistWindow::Rc PlaylistWindow::list_rect() const {
    return { left_w, top_h + list_top, width() - left_w - right_w, height() - top_h - bottom_h - list_top };
}

int PlaylistWindow::visible_rows() const { return std::max(1, list_rect().h / m_row_h); }

int PlaylistWindow::row_at(POINT p) const {
    const Rc r = list_rect();
    if (p.y < r.y) return -1;
    const size_t row = m_scroll + static_cast<size_t>((p.y - r.y) / m_row_h);
    const size_t count = playlist_manager::get()->activeplaylist_get_item_count();
    return row < count ? static_cast<int>(row) : -1;
}

void PlaylistWindow::set_shade(bool on) {
    m_shade = on;
    config::pl_shade = on;
    m_pressed = Part::none;
    apply_geometry();
    layout_host();
}

void PlaylistWindow::save_position() {
    RECT rc;
    if (m_wnd && GetWindowRect(m_wnd, &rc)) on_position_saved(rc.left, rc.top);
}

void PlaylistWindow::on_position_saved(int x, int y) {
    config::pl_x = x;
    config::pl_y = y;
}

void PlaylistWindow::on_visibility_changed(bool visible) {
    config::pl_visible = visible;
    if (m_host) {
        if (visible) ensure_host();
        if (visible) SetTimer(m_wnd, 1, 100, nullptr); // follows the element's scrolling for our thumb
        else KillTimer(m_wnd, 1);
    }
    if (visible && !m_callbacks) m_callbacks = std::make_unique<Callbacks>(*this);
    if (!visible) m_callbacks.reset();
    m_lengths_dirty = true;
    if (MainWindow* w = MainWindow::instance()) w->invalidate();
}

void PlaylistWindow::set_time(bool show, bool negative, int seconds) {
    if (show == m_time_show && negative == m_time_negative && seconds == m_time_seconds) return;
    m_time_show = show;
    m_time_negative = negative;
    m_time_seconds = seconds;
    if (visible() && !m_shade) invalidate();
}

void PlaylistWindow::on_skin_changed() {
    update_font();
    if (m_host) m_host->set_skin(m_skin);
    invalidate();
}

// ---------------------------------------------------------------------------------------------
// hosted element

namespace {
constexpr wchar_t host_class[] = L"foo_winamp_skins_host";

void register_host_class() {
    static bool registered = false;
    if (registered) return;
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = core_api::get_my_instance();
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = host_class;
    if (RegisterClassExW(&wc)) registered = true;
}
} // namespace

void PlaylistWindow::release_element() {
    if (!m_host) return;
    const auto& cfg = m_host->config();
    config::pl_view_config.set(cfg.data(), cfg.size());
    m_host->release();
}

// Main playlist: switches the list area between our list and Default UI's Playlist View.
void PlaylistWindow::set_use_view(bool on) {
    if (on == (m_host != nullptr)) return;
    config::pl_use_view = on;
    if (on) {
        const GUID guid = playlist_view_guid();
        if (guid == pfc::guid_null) {
            FB2K_console_formatter() << "foo_winamp_skins: Default UI's Playlist View element not found";
            config::pl_use_view = false;
            return;
        }
        std::vector<uint8_t> cfg;
        if (auto blob = config::pl_view_config.get(); blob.is_valid() && blob->size()) {
            const auto* p = static_cast<const uint8_t*>(blob->get_ptr());
            cfg.assign(p, p + blob->size());
        }
        m_host = std::make_unique<HostedElement>(guid, std::move(cfg));
        m_host->on_activate = [this] { show(true); };
        if (visible()) {
            ensure_host();
            SetTimer(m_wnd, 1, 100, nullptr);
        }
    } else {
        release_element();
        m_host.reset();
        if (m_container) { DestroyWindow(m_container); m_container = nullptr; }
        if (m_wnd) KillTimer(m_wnd, 1);
        m_clip_menu = Part::none;
        m_last_scroll_pos = -1;
    }
    invalidate();
}

// The element keeps its own size: at double size it fills the scaled list area.
RECT PlaylistWindow::host_rect() const {
    const int s = scale();
    return { left_w * s, top_h * s, (width() - right_w) * s, (height() - bottom_h) * s };
}

void PlaylistWindow::on_double_size_changed() {
    config::pl_double = is_double_size();
    layout_host();
}

void PlaylistWindow::ensure_host() {
    if (!m_host || !m_wnd) return;
    if (!m_container) {
        register_host_class();
        m_container = CreateWindowExW(WS_EX_CONTROLPARENT, host_class, L"", WS_CHILD | WS_CLIPCHILDREN, 0, 0, 0, 0, m_wnd, nullptr,
                                      core_api::get_my_instance(), nullptr);
        if (!m_container) return;
    }
    m_host->set_skin(m_skin);
    if (!m_font) update_font();
    apply_list_font();
    m_host->create(m_container);
    layout_host();
}

void PlaylistWindow::layout_host() {
    if (!m_container || !m_host) return;
    const RECT r = host_rect();
    const int w = std::max<int>(0, r.right - r.left), h = std::max<int>(0, r.bottom - r.top);
    SetWindowPos(m_container, nullptr, r.left, r.top, w, h, SWP_NOZORDER | SWP_NOACTIVATE | (m_shade ? SWP_HIDEWINDOW : SWP_SHOWWINDOW));
    m_host->set_rect({ 0, 0, w + GetSystemMetrics(SM_CXVSCROLL), h });
}

// The sprite menus are drawn on our canvas, under the element's child window: while one is open, its
// rectangle is cut out of the container so the menu shows through.
void PlaylistWindow::update_host_clip() {
    if (!m_container) return;
    const Part menu = (m_pressed >= Part::menu_add && m_pressed <= Part::menu_list) ? m_pressed : Part::none;
    if (menu == m_clip_menu) return;
    m_clip_menu = menu;
    if (menu == Part::none) { SetWindowRgn(m_container, nullptr, TRUE); return; }
    const MenuDef& d = menu_def(static_cast<int>(menu) - static_cast<int>(Part::menu_add));
    const int x = d.from_right ? width() - d.x_from_left : d.x_from_left;
    const int top = height() - 12 - 18 * static_cast<int>(d.items.size());
    const RECT hr = host_rect();
    const int s = scale();
    HRGN rgn = CreateRectRgn(0, 0, hr.right - hr.left, hr.bottom - hr.top);
    HRGN cut = CreateRectRgn((x - 3) * s - hr.left, top * s - hr.top, (x + 22) * s - hr.left, (height() - 12) * s - hr.top);
    CombineRgn(rgn, rgn, cut, RGN_DIFF);
    DeleteObject(cut);
    SetWindowRgn(m_container, rgn, TRUE); // the window owns the region now
}

HWND PlaylistWindow::list_hwnd() const {
    if (!m_container) return nullptr;
    struct Find { HWND found = nullptr; } f;
    EnumChildWindows(m_container, [](HWND w, LPARAM lp) -> BOOL {
        if (GetWindowLongPtrW(w, GWL_STYLE) & WS_VSCROLL) { reinterpret_cast<Find*>(lp)->found = w; return FALSE; }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&f));
    return f.found;
}

bool PlaylistWindow::list_scroll(SCROLLINFO& si) const {
    HWND list = list_hwnd();
    si = {};
    si.cbSize = sizeof(si);
    si.fMask = SIF_ALL;
    return list && GetScrollInfo(list, SB_VERT, &si) && si.nMax >= si.nMin;
}

void PlaylistWindow::update_font() {
    HFONT old = m_font; // deleted after the hosted element has switched to the new one
    m_font = nullptr;
    // Like Winamp: the skin's pledit.txt font unless Options names another; the size is the user's.
    const std::wstring face = wide(config::pl_skin_font.get() ? (m_skin ? m_skin->playlist_colors().font.c_str() : "Arial")
                                                              : config::pl_font.get().get_ptr());
    const int size = std::clamp(static_cast<int>(config::pl_font_size.get()), config::pl_font_size_min, config::pl_font_size_max);
    m_row_h = size + 3;
    LOGFONTW lf = {};
    lf.lfHeight = -size;
    lf.lfWeight = FW_NORMAL;
    lf.lfCharSet = DEFAULT_CHARSET;
    lf.lfQuality = config::pl_font_smooth.get() ? DEFAULT_QUALITY : NONANTIALIASED_QUALITY; // DEFAULT: system smoothing (ClearType)
    wcsncpy_s(lf.lfFaceName, face.c_str(), _TRUNCATE);
    m_font = CreateFontIndirectW(&lf);
    if (m_shade_font) DeleteObject(m_shade_font);
    lf.lfHeight = -config::pl_font_size_default;
    m_shade_font = CreateFontIndirectW(&lf);
    apply_list_font();
    if (old) DeleteObject(old);
    clamp_scroll();
    invalidate();
}

void PlaylistWindow::apply_list_font() {
    if (m_host) m_host->set_list_font(config::playlist_view_skin_font.get() ? m_font : nullptr);
}

void PlaylistWindow::apply_size(int w_extra, int h_extra) {
    w_extra = std::clamp(w_extra, 0, max_extra_w);
    h_extra = std::clamp(h_extra, 0, max_extra_h);
    if (w_extra == m_w_extra && h_extra == m_h_extra) return;
    m_w_extra = w_extra;
    m_h_extra = h_extra;
    config::pl_width = w_extra;
    config::pl_height = h_extra;
    clamp_scroll();
    apply_geometry(false);
    layout_host();
}

void PlaylistWindow::apply_resize_mode() {
    if (config::smooth_resize.get()) return;
    apply_size(m_w_extra - m_w_extra % step_w, m_h_extra - m_h_extra % step_h);
}

void PlaylistWindow::playlist_changed() {
    m_lengths_dirty = true;
    clamp_scroll();
    invalidate();
}

void PlaylistWindow::clamp_scroll() {
    const size_t count = playlist_manager::get()->activeplaylist_get_item_count();
    const size_t rows = static_cast<size_t>(visible_rows());
    const size_t max = count > rows ? count - rows : 0;
    m_scroll = std::min(m_scroll, max);
}

void PlaylistWindow::ensure_visible(size_t item) {
    const size_t rows = static_cast<size_t>(visible_rows());
    if (item < m_scroll) m_scroll = item;
    else if (item >= m_scroll + rows) m_scroll = item - rows + 1;
    clamp_scroll();
}

void PlaylistWindow::recompute_lengths() {
    m_lengths_dirty = false;
    m_total_len = m_sel_len = 0;
    auto pm = playlist_manager::get();
    const size_t n = pm->activeplaylist_get_item_count();
    pfc::bit_array_bittable sel(n);
    pm->activeplaylist_get_selection_mask(sel);
    for (size_t i = 0; i < n; ++i) {
        const double len = std::floor(std::max(0.0, pm->activeplaylist_get_item_handle(i)->get_length())); // sum of shown lengths
        m_total_len += len;
        if (sel.get(i)) m_sel_len += len;
    }
}

// ---------------------------------------------------------------------------------------------
// hit testing

// Hit areas follow Winamp's playlist cursor map.
SkinCursor PlaylistWindow::cursor_at(POINT p) const {
    const SIZE sz = skin_size();
    const int w = sz.cx, h = sz.cy;
    if (m_shade) {
        if (in_rect(p, w - 20, 3, w - 12, 12)) return SkinCursor::plws_winbut;
        if (in_rect(p, w - 11, 3, w - 3, 12)) return SkinCursor::plws_close;
        if (in_rect(p, w - 29, 3, w - 20, 12)) return SkinCursor::plws_size;
        return SkinCursor::plws_normal;
    }
    if (in_rect(p, w - 20, 3, w - 12, 12)) return SkinCursor::pl_winbut;
    if (in_rect(p, w - 11, 3, w - 3, 12)) return SkinCursor::pl_close;
    if (in_rect(p, 0, 0, w - 1, 13)) return SkinCursor::pl_titlebar;
    if (in_rect(p, w - 15, 20, w - 7, h - 38)) return SkinCursor::pl_vscroll;
    if (in_rect(p, w - 20, h - 20, w - 1, h - 1)) return SkinCursor::pl_size;
    return SkinCursor::pl_normal;
}

const wchar_t* PlaylistWindow::tooltip_at(POINT p) const {
    if (m_menu_open) return nullptr;
    switch (hit(p)) {
    case Part::close: return L"Close";
    case Part::shade: return L"Toggle Windowshade Mode";
    case Part::menu_add: return L"Add Files to Playlist";
    case Part::menu_rem: return L"Remove Files from Playlist";
    case Part::menu_sel: return L"Select Files in Playlist";
    case Part::menu_misc: return L"Misc Actions";
    case Part::menu_list: return L"Manage Playlist";
    case Part::scroll_up: return L"Scroll Up";
    case Part::scroll_down: return L"Scroll Down";
    case Part::resize: return m_shade ? L"Adjust Horizontal Size" : nullptr;
    case Part::prev: return L"Previous Track";
    case Part::play: return L"Play";
    case Part::pause: return L"Pause";
    case Part::stop: return L"Stop";
    case Part::next: return L"Next Track";
    case Part::eject: return L"Open file(s)";
    case Part::mini_time: return L"Time Display (click to toggle elapsed/remaining)";
    default: return nullptr;
    }
}

PlaylistWindow::Part PlaylistWindow::hit(POINT p) const {
    const int w = width(), h = height();
    if (Rc{ w - 11, 3, 9, 9 }.in(p)) return Part::close;
    if (Rc{ w - 20, 3, 9, 9 }.in(p)) return Part::shade;
    if (m_shade) {
        if (Rc{ w - 29, 3, 9, 9 }.in(p)) return Part::resize;
        return Part::none;
    }
    if (list_rect().in(p)) return Part::list;
    if (Rc{ w - 15, top_h, 8, h - top_h - bottom_h }.in(p)) return Part::scrollbar;
    if (Rc{ w - 20, h - 20, 20, 20 }.in(p)) return Part::resize;
    const int by = h - bottom_h, bx = w - 150;
    for (int i = 0; i < 5; ++i) {
        const MenuDef& d = menu_def(i);
        const int x = d.from_right ? w - d.x_from_left : d.x_from_left;
        if (Rc{ x, h - 30, 22, 18 }.in(p)) return static_cast<Part>(static_cast<int>(Part::menu_add) + i);
    }
    static constexpr Part buttons[] = { Part::prev, Part::play, Part::pause, Part::stop, Part::next, Part::eject };
    for (int i = 0; i < 6; ++i)
        if (Rc{ bx + 3 + i * 10, by + 22, 10, 10 }.in(p)) return buttons[i];
    if (Rc{ w - 15, by + 2, 8, 5 }.in(p)) return Part::scroll_up;
    if (Rc{ w - 15, by + 8, 8, 5 }.in(p)) return Part::scroll_down;
    if (Rc{ bx + 66, by + 23, 25, 6 }.in(p)) return Part::mini_time;
    return Part::none;
}

bool PlaylistWindow::is_control(POINT p) const {
    return m_pressed != Part::none || hit(p) != Part::none;
}

// ---------------------------------------------------------------------------------------------
// mouse

static int menu_index(int part, int first) { return part - first; }

void PlaylistWindow::on_mouse_down(POINT p, bool dbl) {
    if (m_menu_open) {
        // Second click of click mode: pick the item under the cursor, or close the menu.
        const Part part = m_pressed;
        const int hover = menu_hover_at(p);
        m_menu_open = false;
        m_pressed = Part::none;
        m_menu_hover = -1;
        update_host_clip();
        invalidate();
        if (hover >= 0) run_menu_action(part, hover);
        return;
    }
    m_pressed = hit(p);
    update_host_clip();
    m_inside = true;
    m_menu_hover = -1;
    m_drag_reorder = false;
    auto pm = playlist_manager::get();
    switch (m_pressed) {
    case Part::list: {
        const int row = row_at(p);
        if (row < 0) {
            pm->activeplaylist_clear_selection();
            m_pressed = Part::none;
            break;
        }
        const size_t item = static_cast<size_t>(row);
        if (dbl) {
            pm->activeplaylist_execute_default_action(item);
            m_pressed = Part::none;
            break;
        }
        const bool ctrl = GetKeyState(VK_CONTROL) < 0, shift = GetKeyState(VK_SHIFT) < 0;
        const size_t count = pm->activeplaylist_get_item_count();
        if (shift && m_anchor < count) {
            const size_t a = std::min(m_anchor, item), b = std::max(m_anchor, item);
            pm->activeplaylist_set_selection(pfc::bit_array_true(), pfc::bit_array_range(a, b - a + 1));
        } else if (ctrl) {
            pm->activeplaylist_set_selection_single(item, !pm->activeplaylist_is_item_selected(item));
            m_anchor = item;
        } else {
            // A selected row keeps the selection for dragging it; a click without a drag reduces it on release.
            if (!pm->activeplaylist_is_item_selected(item))
                pm->activeplaylist_set_selection(pfc::bit_array_true(), pfc::bit_array_one(item));
            else
                m_reduce_to = item;
            m_anchor = item;
            m_drag_row = row;
            m_drag_reorder = true;
        }
        pm->activeplaylist_set_focus_item(item);
        break;
    }
    case Part::scrollbar: {
        const int thumb_y = thumb_top();
        m_thumb_grab = (p.y >= thumb_y && p.y < thumb_y + scroll_handle.h) ? p.y - thumb_y : scroll_handle.h / 2;
        scroll_to_thumb(p.y);
        break;
    }
    case Part::resize:
        GetCursorPos(&m_drag_origin);
        m_drag_extra = { m_w_extra, m_h_extra };
        break;
    default:
        break;
    }
    invalidate();
}

// Thumb position: our list's scroll offset, or the hosted element's scrollbar.
int PlaylistWindow::thumb_top() const {
    const int track_h = height() - top_h - bottom_h - scroll_handle.h;
    if (m_host) {
        SCROLLINFO si;
        if (!list_scroll(si)) return top_h;
        const int range = si.nMax + 1 - si.nMin - static_cast<int>(si.nPage);
        return top_h + (range > 0 ? static_cast<int>(track_h * double(si.nPos - si.nMin) / range) : 0);
    }
    const size_t count = playlist_manager::get()->activeplaylist_get_item_count();
    const size_t rows = static_cast<size_t>(visible_rows());
    return top_h + (count > rows ? static_cast<int>(track_h * double(m_scroll) / double(count - rows)) : 0);
}

void PlaylistWindow::scroll_to_thumb(int y) {
    const int track_h = height() - top_h - bottom_h - scroll_handle.h;
    if (m_host) {
        SCROLLINFO si;
        if (!list_scroll(si) || track_h <= 0) return;
        const int range = si.nMax + 1 - si.nMin - static_cast<int>(si.nPage);
        if (range <= 0) return;
        const double t = std::clamp((y - m_thumb_grab - top_h) / double(track_h), 0.0, 1.0);
        HWND list = list_hwnd();
        SCROLLINFO pos = {};
        pos.cbSize = sizeof(pos);
        pos.fMask = SIF_POS;
        pos.nPos = si.nMin + static_cast<int>(std::lround(t * range));
        SetScrollInfo(list, SB_VERT, &pos, FALSE);
        SendMessageW(list, WM_VSCROLL, MAKEWPARAM(SB_THUMBTRACK, pos.nPos & 0xFFFF), 0);
        SendMessageW(list, WM_VSCROLL, MAKEWPARAM(SB_ENDSCROLL, 0), 0);
        return;
    }
    const size_t count = playlist_manager::get()->activeplaylist_get_item_count();
    const size_t rows = static_cast<size_t>(visible_rows());
    if (count <= rows || track_h <= 0) { m_scroll = 0; return; }
    const double t = std::clamp((y - m_thumb_grab - top_h) / double(track_h), 0.0, 1.0);
    m_scroll = static_cast<size_t>(std::lround(t * double(count - rows)));
}

void PlaylistWindow::on_mouse_move(POINT p) {
    switch (m_pressed) {
    case Part::none:
        return;
    case Part::list: {
        if (!m_drag_reorder) return;
        const Rc r = list_rect();
        const int count = static_cast<int>(playlist_manager::get()->activeplaylist_get_item_count());
        const int row = std::clamp(static_cast<int>(m_scroll) + static_cast<int>((p.y - r.y) / m_row_h) - (p.y < r.y ? 1 : 0), 0, std::max(0, count - 1));
        if (row != m_drag_row) {
            m_reduce_to = SIZE_MAX;
            playlist_manager::get()->activeplaylist_move_selection(row - m_drag_row);
            m_drag_row = row;
            ensure_visible(static_cast<size_t>(row));
        }
        break;
    }
    case Part::scrollbar:
        scroll_to_thumb(p.y);
        break;
    case Part::resize: {
        POINT now;
        GetCursorPos(&now);
        const int dx = (now.x - m_drag_origin.x) / scale(), dy = (now.y - m_drag_origin.y) / scale();
        // Steps of 25x29 px like Winamp, or per pixel with smooth resizing.
        const bool smooth = config::smooth_resize.get();
        auto step = [&](int d, int s) { return smooth ? d : static_cast<int>(std::floor(d / double(s) + 0.5)) * s; };
        const int we = m_drag_extra.cx + step(dx, step_w);
        const int he = m_shade ? m_h_extra : m_drag_extra.cy + step(dy, step_h);
        apply_size(we, he);
        break;
    }
    case Part::menu_add: case Part::menu_rem: case Part::menu_sel: case Part::menu_misc: case Part::menu_list:
        m_menu_hover = menu_hover_at(p);
        break;
    default:
        m_inside = hit(p) == m_pressed;
        break;
    }
    invalidate();
}

int PlaylistWindow::menu_hover_at(POINT p) const {
    const MenuDef& d = menu_def(menu_index(static_cast<int>(m_pressed), static_cast<int>(Part::menu_add)));
    const int x = d.from_right ? width() - d.x_from_left : d.x_from_left;
    const int n = static_cast<int>(d.items.size());
    const int top = height() - 12 - 18 * n;
    return (p.x >= x && p.x < x + 22 && p.y >= top && p.y < top + 18 * n) ? (p.y - top) / 18 : -1;
}

void PlaylistWindow::on_mouse_up(POINT p) {
    // Click mode (Winamp's default): releasing the button leaves the menu open; the next click picks.
    if (m_pressed >= Part::menu_add && m_pressed <= Part::menu_list && !m_menu_open && !config::oldschool_pl_buttons.get()) {
        m_menu_open = true;
        m_menu_hover = menu_hover_at(p);
        invalidate();
        return;
    }
    const Part part = m_pressed;
    m_pressed = Part::none;
    if (part == Part::list && m_reduce_to != SIZE_MAX) {
        auto pm = playlist_manager::get();
        if (m_reduce_to < pm->activeplaylist_get_item_count())
            pm->activeplaylist_set_selection(pfc::bit_array_true(), pfc::bit_array_one(m_reduce_to));
    }
    m_reduce_to = SIZE_MAX;
    update_host_clip();
    const bool inside = hit(p) == part;
    const int hover = m_menu_hover;
    m_menu_hover = -1;
    invalidate();
    switch (part) {
    case Part::close: if (inside) show(false); break;
    case Part::shade: if (inside) set_shade(!m_shade); break;
    case Part::prev: if (inside) player::prev_button(); break;
    case Part::play: if (inside) player::play(); break;
    case Part::pause: if (inside) player::pause(); break;
    case Part::stop: if (inside) player::stop_button(); break;
    case Part::next: if (inside) player::next_button(); break;
    case Part::eject: if (inside) player::open_files(); break;
    case Part::mini_time: if (inside) config::time_remaining = !config::time_remaining.get(); break;
    case Part::scroll_up:
        if (!inside) break;
        if (HWND list = list_hwnd()) SendMessageW(list, WM_VSCROLL, MAKEWPARAM(SB_LINEUP, 0), 0);
        else if (!m_host) m_scroll = m_scroll > 0 ? m_scroll - 1 : 0;
        break;
    case Part::scroll_down:
        if (!inside) break;
        if (HWND list = list_hwnd()) SendMessageW(list, WM_VSCROLL, MAKEWPARAM(SB_LINEDOWN, 0), 0);
        else if (!m_host) { ++m_scroll; clamp_scroll(); }
        break;
    case Part::resize: save_position(); break;
    case Part::menu_add: case Part::menu_rem: case Part::menu_sel: case Part::menu_misc: case Part::menu_list:
        if (hover >= 0) run_menu_action(part, hover);
        break;
    default: break;
    }
    if (MainWindow* w = MainWindow::instance()) w->invalidate();
}

void PlaylistWindow::on_mouse_leave_capture() {
    m_menu_open = false;
    m_reduce_to = SIZE_MAX;
    m_pressed = Part::none;
    update_host_clip();
    m_menu_hover = -1;
    invalidate();
}

void PlaylistWindow::on_wheel(int delta) {
    // Windowshade: the wheel changes track, like Winamp (up = previous).
    if (m_shade) { if (delta > 0) player::previous(); else player::next(); return; }
    if (m_host) {
        if (HWND list = list_hwnd()) SendMessageW(list, WM_MOUSEWHEEL, MAKEWPARAM(0, delta), 0);
        return;
    }
    const int lines = delta > 0 ? -3 : 3;
    if (lines < 0) m_scroll = m_scroll > size_t(-lines) ? m_scroll + lines : 0;
    else m_scroll += size_t(lines);
    clamp_scroll();
    invalidate();
}

void PlaylistWindow::on_right_click(POINT screen_pt) {
    POINT client = screen_pt;
    ScreenToClient(m_wnd, &client);
    const POINT p = to_skin(client);
    if (!m_host && !m_shade && list_rect().in(p)) {
        const int row = row_at(p);
        auto pm = playlist_manager::get();
        if (row >= 0 && !pm->activeplaylist_is_item_selected(static_cast<size_t>(row))) {
            pm->activeplaylist_set_selection(pfc::bit_array_true(), pfc::bit_array_one(static_cast<size_t>(row)));
            pm->activeplaylist_set_focus_item(static_cast<size_t>(row));
        }
        metadb_handle_list items;
        pm->activeplaylist_get_selected_items(items);
        if (items.get_count()) contextmenu_manager::win32_run_menu_context(m_wnd, items, &screen_pt);
        return;
    }
    SkinWindow::on_right_click(screen_pt);
}

void PlaylistWindow::run_menu_action(Part menu, int item) {
    auto pm = playlist_manager::get();
    const size_t n = pm->activeplaylist_get_item_count();
    switch (menu) {
    case Part::menu_add:
        if (item == 0) standard_commands::main_add_location();
        else if (item == 1) standard_commands::main_add_directory();
        else standard_commands::main_add_files();
        break;
    case Part::menu_rem:
        if (item == 0) {
            enum : UINT { id_dead = 1, id_dupes };
            HMENU m = CreatePopupMenu();
            AppendMenuW(m, MF_STRING, id_dead, L"Remove &dead items");
            AppendMenuW(m, MF_STRING, id_dupes, L"Remove d&uplicates");
            POINT pt; GetCursorPos(&pt);
            const UINT cmd = TrackPopupMenu(m, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_NONOTIFY, pt.x, pt.y, 0, m_wnd, nullptr);
            DestroyMenu(m);
            if (cmd == id_dead) standard_commands::main_remove_dead_entries();
            else if (cmd == id_dupes) standard_commands::main_remove_duplicates();
        } else if (item == 1) pm->activeplaylist_clear();
        else if (item == 2) pm->activeplaylist_remove_selection(true);
        else pm->activeplaylist_remove_selection(false);
        break;
    case Part::menu_sel:
        if (item == 0) {
            pfc::bit_array_bittable sel(n);
            pm->activeplaylist_get_selection_mask(sel);
            pm->activeplaylist_set_selection(pfc::bit_array_true(), pfc::bit_array_not(sel));
        } else if (item == 1) pm->activeplaylist_clear_selection();
        else pm->activeplaylist_set_selection(pfc::bit_array_true(), pfc::bit_array_true());
        break;
    case Part::menu_misc:
        if (item == 0) {
            enum : UINT { id_title = 1, id_file, id_path, id_reverse, id_random };
            HMENU m = CreatePopupMenu();
            AppendMenuW(m, MF_STRING, id_title, L"Sort list by &title");
            AppendMenuW(m, MF_STRING, id_file, L"Sort list by &filename");
            AppendMenuW(m, MF_STRING, id_path, L"Sort list by &path and filename");
            AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
            AppendMenuW(m, MF_STRING, id_reverse, L"&Reverse list");
            AppendMenuW(m, MF_STRING, id_random, L"R&andomize list");
            POINT pt; GetCursorPos(&pt);
            const UINT cmd = TrackPopupMenu(m, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_NONOTIFY, pt.x, pt.y, 0, m_wnd, nullptr);
            DestroyMenu(m);
            if (cmd == id_title) pm->activeplaylist_sort_by_format("%title%", false);
            else if (cmd == id_file) pm->activeplaylist_sort_by_format("%filename_ext%", false);
            else if (cmd == id_path) pm->activeplaylist_sort_by_format("%path%", false);
            else if (cmd == id_random) pm->activeplaylist_sort_by_format(nullptr, false);
            else if (cmd == id_reverse && n > 1) {
                std::vector<size_t> order(n);
                for (size_t i = 0; i < n; ++i) order[i] = n - 1 - i;
                pm->activeplaylist_reorder_items(order.data(), n);
            }
        } else if (item == 1) {
            metadb_handle_list items;
            pm->activeplaylist_get_selected_items(items);
            if (items.get_count()) standard_commands::context_file_properties(items);
        } else {
            POINT pt; GetCursorPos(&pt);
            contextmenu_manager::win32_run_menu_context_playlist(m_wnd, &pt);
        }
        break;
    case Part::menu_list:
        if (item == 0) standard_commands::main_create_playlist();
        else if (item == 1) standard_commands::main_save_playlist();
        else standard_commands::main_load_playlist();
        break;
    default:
        break;
    }
}

// ---------------------------------------------------------------------------------------------
// keyboard / messages

void PlaylistWindow::move_focus(int delta, bool extend) {
    auto pm = playlist_manager::get();
    const size_t n = pm->activeplaylist_get_item_count();
    if (!n) return;
    size_t focus = pm->activeplaylist_get_focus_item();
    if (focus >= n) focus = 0;
    const long long target = std::clamp<long long>(static_cast<long long>(focus) + delta, 0, static_cast<long long>(n) - 1);
    const size_t item = static_cast<size_t>(target);
    if (extend) {
        if (m_anchor >= n) m_anchor = focus;
        const size_t a = std::min(m_anchor, item), b = std::max(m_anchor, item);
        pm->activeplaylist_set_selection(pfc::bit_array_true(), pfc::bit_array_range(a, b - a + 1));
    } else {
        pm->activeplaylist_set_selection(pfc::bit_array_true(), pfc::bit_array_one(item));
        m_anchor = item;
    }
    pm->activeplaylist_set_focus_item(item);
    ensure_visible(item);
    invalidate();
}

bool PlaylistWindow::on_key(WPARAM vk) {
    if (m_shade) {
        // Windowshade: the list is hidden, so up/down change track instead; other keys go to foobar2000.
        if (vk == VK_UP) { player::previous(); return true; }
        if (vk == VK_DOWN) { player::next(); return true; }
        return false;
    }
    if (m_host) return false; // the element handles its own keys
    const bool shift = GetKeyState(VK_SHIFT) < 0, ctrl = GetKeyState(VK_CONTROL) < 0;
    auto pm = playlist_manager::get();
    const int page = std::max(1, visible_rows() - 1);
    switch (vk) {
    case VK_UP: move_focus(-1, shift); return true;
    case VK_DOWN: move_focus(1, shift); return true;
    case VK_PRIOR: move_focus(-page, shift); return true;
    case VK_NEXT: move_focus(page, shift); return true;
    case VK_HOME: move_focus(-0x3fffffff, shift); return true;
    case VK_END: move_focus(0x3fffffff, shift); return true;
    case VK_RETURN: {
        const size_t focus = pm->activeplaylist_get_focus_item();
        if (focus < pm->activeplaylist_get_item_count()) pm->activeplaylist_execute_default_action(focus);
        return true;
    }
    case VK_DELETE: pm->activeplaylist_remove_selection(false); return true;
    case 'A':
        if (!ctrl) return false;
        pm->activeplaylist_set_selection(pfc::bit_array_true(), pfc::bit_array_true());
        return true;
    }
    return false;
}

bool PlaylistWindow::on_message(UINT msg, WPARAM wp, LPARAM, LRESULT& ret) {
    if (msg == WM_TIMER && wp == 1 && m_host) {
        SCROLLINFO si;
        const int pos = list_scroll(si) ? si.nPos : -1;
        if (pos != m_last_scroll_pos) { m_last_scroll_pos = pos; invalidate(); }
        ret = 0;
        return true;
    }
    if (msg == WM_SIZE) layout_host();
    if (msg == WM_CREATE) {
        DragAcceptFiles(m_wnd, TRUE);
        return false;
    }
    if (msg == WM_DROPFILES) {
        HDROP drop = reinterpret_cast<HDROP>(wp);
        const UINT count = DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);
        pfc::list_t<pfc::string8> paths;
        for (UINT i = 0; i < count; ++i) {
            const UINT len = DragQueryFileW(drop, i, nullptr, 0);
            std::wstring buf(len + 1, L'\0');
            DragQueryFileW(drop, i, buf.data(), len + 1);
            buf.resize(len);
            paths.add_item(pfc::stringcvt::string_utf8_from_wide(buf.c_str()).get_ptr());
        }
        DragFinish(drop);
        pfc::list_t<const char*> ptrs;
        for (size_t i = 0; i < paths.get_count(); ++i) ptrs.add_item(paths[i].get_ptr());
        if (ptrs.get_count()) playlist_manager::get()->activeplaylist_add_locations(ptrs, true, m_wnd);
        ret = 0;
        return true;
    }
    return false;
}

// ---------------------------------------------------------------------------------------------
// rendering

void PlaylistWindow::render(Canvas& c) {
    if (!m_font) update_font();
    if (m_shade) render_shade(c);
    else render_normal(c);
}

void PlaylistWindow::render_normal(Canvas& c) {
    const Image& img = m_skin->bitmap(SkinBitmap::pledit);
    const int w = width(), h = height();
    const bool a = title_lit();
    c.clear(0);

    // Top: corners, title centred, tiles between.
    c.tile(img, a ? top_tile_sel : top_tile, 25, 0, w - 50, top_h);
    c.blit(img, a ? top_left_sel : top_left, { 0, 0 });
    c.blit(img, a ? top_title_sel : top_title, { (w - 100) / 2, 0 });
    c.blit(img, a ? top_right_sel : top_right, { w - 25, 0 });
    // Title buttons always come from the active row when not pressed.
    // Unpressed title buttons are part of the title row.
    if (m_pressed == Part::close && m_inside) c.blit(img, close_down, { w - 11, 3 });
    if (m_pressed == Part::shade && m_inside) c.blit(img, collapse_down, { w - 20, 3 });

    // Sides.
    c.tile(img, left_tile, 0, top_h, left_w, h - top_h - bottom_h);
    c.tile(img, right_tile, w - right_w, top_h, right_w, h - top_h - bottom_h);

    // Bottom: tiles, the last one stretched. Winamp's playlist visualizer piece is left out on purpose.
    {
        const int n = (w - 275) / 25;
        int x = 125;
        for (int i = 0; i < n; ++i, x += 25) c.blit(img, bottom_tile, { x, h - bottom_h });
        if (const int rest = (w - 275) % 25; rest > 0) {
            for (int dx = 0; dx < rest; ++dx) // StretchBlt of the 25 px tile onto `rest` columns
                c.blit(img, bottom_tile.x + dx * bottom_tile.w / rest, bottom_tile.y, 1, bottom_h, x + dx, h - bottom_h);
        }
    }
    c.blit(img, bottom_left, { 0, h - bottom_h });
    c.blit(img, bottom_right, { w - 150, h - bottom_h });

    if (!m_host) render_list(c);

    // Scrollbar thumb.
    c.blit(img, m_pressed == Part::scrollbar ? scroll_handle_down : scroll_handle, { w - 15, thumb_top() });

    // Running time (selected/total) and mini time, in the skin's text font.
    const Image& font = m_skin->bitmap(SkinBitmap::text);
    if (m_lengths_dirty) recompute_lengths();
    const int bx = w - 150, by = h - bottom_h;
    const pfc::string8 running = pfc::format(time_str(m_sel_len, true), "/", time_str(m_total_len, true));
    c.set_clip(bx + 7, by + 10, 18 * font_char_w, font_char_h);
    draw_text(c, font, bx + 7, by + 10, running);
    c.reset_clip();

    // Mini time, laid out like Winamp around the colon printed in pledit.bmp: [-][m][m][m] : [s][s].
    if (m_time_show) {
        const int x = w - 86, y = h - 15;
        const int m = std::min(m_time_seconds / 60, 999), s = m_time_seconds % 60;
        auto digit = [&](int d, int dx) { c.blit(font, 5 * d, 6, 5, 6, x + dx, y); };
        if (m < 100) c.blit(font, 142, 0, 4, 6, x, y);
        c.blit(font, m_time_negative ? 75 : 142, m_time_negative ? 6 : 0, 3, 6, x + (m >= 100 ? 0 : 4), y);
        if (m >= 100) digit(m / 100, 4);
        digit(m / 10 % 10, 9);
        digit(m % 10, 14);
        digit(s / 10, 22);
        digit(s % 10, 27);
    }

    render_menu(c);
}

void PlaylistWindow::render_list(Canvas& c) {
    const PlaylistColors& col = m_skin->playlist_colors();
    const Rc r = list_rect();
    c.fill_rect(r.x, r.y - list_top, r.w, r.h + list_top, to_bgrx(col.normal_bg));

    auto pm = playlist_manager::get();
    const size_t count = pm->activeplaylist_get_item_count();
    size_t playing_pl = SIZE_MAX, playing_idx = SIZE_MAX;
    pm->get_playing_item_location(&playing_pl, &playing_idx);
    const bool playing_here = playing_pl == pm->get_active_playlist();
    const size_t focus = pm->activeplaylist_get_focus_item();
    const size_t rows = static_cast<size_t>(visible_rows());

    HDC dc = c.dc();
    c.set_clip(r.x, r.y, r.w, static_cast<int>(rows) * m_row_h);
    HGDIOBJ old_font = SelectObject(dc, m_font);
    SetBkMode(dc, TRANSPARENT);
    const bool has_focus = GetFocus() == m_wnd;
    for (size_t i = 0; i < rows && m_scroll + i < count; ++i) {
        const size_t item = m_scroll + i;
        const int y = r.y + static_cast<int>(i) * m_row_h;
        const bool selected = pm->activeplaylist_is_item_selected(item);
        if (selected) {
            GdiFlush();
            c.fill_rect(r.x, y, r.w, m_row_h, to_bgrx(col.selected_bg));
        }
        pfc::string8 title;
        pm->activeplaylist_item_format_title(item, nullptr, title, m_title_script, nullptr, playback_control::display_level_titles);
        const double len = pm->activeplaylist_get_item_handle(item)->get_length();
        const std::wstring left = wide(title);
        const std::wstring right = len > 0 ? wide(time_str(len, true)) : std::wstring();

        SetTextColor(dc, playing_here && item == playing_idx ? col.current : col.normal);
        RECT tr = { r.x, y, r.x + r.w - 3, y + m_row_h };
        SIZE rs = {};
        if (!right.empty()) {
            GetTextExtentPoint32W(dc, right.c_str(), static_cast<int>(right.size()), &rs);
            DrawTextW(dc, right.c_str(), static_cast<int>(right.size()), &tr, DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        }
        RECT lr = { tr.left, y, tr.right - (rs.cx ? rs.cx + 4 : 0), y + m_row_h };
        DrawTextW(dc, left.c_str(), static_cast<int>(left.size()), &lr, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
        if (item == focus && has_focus) {
            RECT fr = { r.x, y, r.x + r.w, y + m_row_h };
            SetTextColor(dc, col.normal);
            DrawFocusRect(dc, &fr);
        }
    }
    SelectObject(dc, old_font);
    GdiFlush();
    c.reset_clip();
}

void PlaylistWindow::render_menu(Canvas& c) {
    if (m_pressed < Part::menu_add || m_pressed > Part::menu_list) return;
    const Image& img = m_skin->bitmap(SkinBitmap::pledit);
    const MenuDef& d = menu_def(menu_index(static_cast<int>(m_pressed), static_cast<int>(Part::menu_add)));
    const int x = d.from_right ? width() - d.x_from_left : d.x_from_left;
    const int n = static_cast<int>(d.items.size());
    const int top = height() - 12 - 18 * n;
    c.blit(img, d.bar, { x - 3, top });
    for (int i = 0; i < n; ++i) c.blit(img, i == m_menu_hover ? d.items[i].hover : d.items[i].normal, { x, top + 18 * i });
}

void PlaylistWindow::render_shade(Canvas& c) {
    const Image& img = m_skin->bitmap(SkinBitmap::pledit);
    const Image& font = m_skin->bitmap(SkinBitmap::text);
    const int w = width();
    c.clear(0);
    c.tile(img, shade_tile, 25, 0, w - 75, shade_height);
    c.blit(img, shade_left, { 0, 0 });
    c.blit(img, title_lit() ? shade_right_sel : shade_right, { w - 50, 0 });
    if (m_pressed == Part::close && m_inside) c.blit(img, close_down, { w - 11, 3 });
    if (m_pressed == Part::shade && m_inside) c.blit(img, expand_down, { w - 20, 3 });

    // Current (or focused) track and its time.
    auto pc = playback_control::get();
    auto pm = playlist_manager::get();
    pfc::string8 title;
    double len = 0;
    metadb_handle_ptr track;
    if (pc->get_now_playing(track)) {
        pc->playback_format_title(nullptr, title, m_shade_script, nullptr, playback_control::display_level_titles);
        len = track->get_length();
    } else {
        const size_t focus = pm->activeplaylist_get_focus_item();
        if (focus < pm->activeplaylist_get_item_count()) {
            pm->activeplaylist_item_format_title(focus, nullptr, title, m_shade_script, nullptr, playback_control::display_level_none);
            len = pm->activeplaylist_get_item_handle(focus)->get_length();
        }
    }
    if (!config::title_bitmap_font.get()) {
        // Like Winamp: the playlist font and colours over x 4..w-29, time right-aligned as " m:ss ".
        const PlaylistColors& col = m_skin->playlist_colors();
        const int right = w - 29;
        c.fill_rect(4, 2, right - 4, 10, to_bgrx(col.normal_bg));
        HDC dc = c.dc();
        const int saved = SaveDC(dc);
        IntersectClipRect(dc, 4, 2, right, 12);
        SelectObject(dc, m_shade_font);
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, col.normal);
        int text_right = right;
        if (len > 0) {
            const std::wstring t = wide(pfc::format(" ", time_str(len, false), " ").c_str());
            RECT tr = { 4, 1, right, 11 };
            DrawTextW(dc, t.c_str(), -1, &tr, DT_RIGHT | DT_SINGLELINE | DT_NOPREFIX);
            RECT m = {};
            DrawTextW(dc, t.c_str(), -1, &m, DT_CALCRECT | DT_SINGLELINE | DT_NOPREFIX);
            text_right -= m.right - m.left;
        }
        RECT r = { 4, 2, text_right, 12 };
        const std::wstring wt = wide(title.c_str());
        DrawTextW(dc, wt.c_str(), -1, &r, DT_LEFT | DT_SINGLELINE | DT_NOPREFIX);
        RestoreDC(dc, saved);
        GdiFlush();
        return;
    }
    const int time_x = w - 29 - 5 * font_char_w; // time right-aligned to w-29
    c.set_clip(4, 4, time_x - 7, font_char_h);
    draw_text(c, font, 4, 4, title);
    c.reset_clip();
    if (len > 0) {
        const pfc::string8 t = time_str(len, false);
        draw_text(c, font, time_x + static_cast<int>(5 - std::min<size_t>(5, t.length())) * font_char_w, 4, t);
    }
}

} // namespace wsz
