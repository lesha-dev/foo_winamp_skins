#include "stdafx.h"
#include "art_window.h"
#include "config.h"
#include "main_window.h"

#include <cmath>

namespace wsz {
namespace {

constexpr int max_size = 4000;

// Default UI's "Album Art Viewer" element (foo_ui_std), found by name so no private GUID is assumed.
ui_element::ptr find_album_art_element() {
    for (auto e : ui_element::enumerate()) {
        pfc::string8 name;
        e->get_name(name);
        if (strcmp(name, "Album Art Viewer") == 0) return e;
    }
    FB2K_console_formatter() << "foo_winamp_skins: Default UI's Album Art Viewer element not found";
    return nullptr;
}

} // namespace

ArtWindow::ArtWindow() {
    m_w = std::clamp(static_cast<int>(config::art_width.get()), GenFrame::min_width, max_size);
    m_h = std::clamp(static_cast<int>(config::art_height.get()), GenFrame::min_height, max_size);
    if (!config::smooth_resize.get()) { m_w -= m_w % GenFrame::step_w; m_h -= (m_h - GenFrame::min_height) % GenFrame::step_h; }
}

ArtWindow::~ArtWindow() {
    release_element();
    ui_element_instance_callback_release();
}

void ArtWindow::save_position() {
    RECT rc;
    if (m_wnd && GetWindowRect(m_wnd, &rc)) on_position_saved(rc.left, rc.top);
}

void ArtWindow::on_position_saved(int x, int y) {
    config::art_x = x;
    config::art_y = y;
}

void ArtWindow::on_visibility_changed(bool visible) {
    config::art_visible = visible;
    if (visible) ensure_element();
    if (MainWindow* w = MainWindow::instance()) w->invalidate();
}

void ArtWindow::on_skin_changed() {
    layout_element();
    if (m_elem.is_valid()) m_elem->notify(ui_element_notify_colors_changed, 0, nullptr, 0);
    invalidate();
}

RECT ArtWindow::content_rect() const {
    const GenFrame::Insets in = m_skin ? GenFrame::insets(*m_skin) : GenFrame::Insets{ 0, 0, 0, 0 };
    return { in.left, in.top, std::max<int>(in.left, m_w - in.right), std::max<int>(in.top, m_h - in.bottom) };
}

void ArtWindow::render(Canvas& c) {
    GenFrame::render(c, *m_skin, m_w, m_h, "Album Art", title_lit(), m_pressed == Part::close && m_inside);
}

ArtWindow::Part ArtWindow::hit(POINT p) const {
    if (in_rect(p, m_w - 11, 3, m_w - 3, 12)) return Part::close;
    if (p.x >= m_w - 20 && p.y >= m_h - 20) return Part::resize;
    return Part::none;
}

// Same map Winamp uses for its generic windows (playlist cursors).
SkinCursor ArtWindow::cursor_at(POINT p) const {
    if (in_rect(p, m_w - 11, 3, m_w - 3, 12)) return SkinCursor::pl_close;
    if (in_rect(p, 0, 0, m_w - 1, 13)) return SkinCursor::pl_titlebar;
    if (in_rect(p, m_w - 20, m_h - 20, m_w - 1, m_h - 1)) return SkinCursor::pl_size;
    return SkinCursor::pl_normal;
}

void ArtWindow::on_mouse_down(POINT p, bool) {
    m_pressed = hit(p);
    m_inside = true;
    if (m_pressed == Part::resize) {
        GetCursorPos(&m_drag_origin);
        m_drag_size = { m_w, m_h };
    }
    invalidate();
}

void ArtWindow::on_mouse_move(POINT p) {
    switch (m_pressed) {
    case Part::close: {
        const bool inside = hit(p) == Part::close;
        if (inside != m_inside) { m_inside = inside; invalidate(); }
        break;
    }
    case Part::resize: {
        POINT now;
        GetCursorPos(&now);
        // Steps of 25x29 px like Winamp, or per pixel with smooth resizing.
        const bool smooth = config::smooth_resize.get();
        auto step = [&](int d, int s) { return smooth ? d : static_cast<int>(std::floor(d / double(s) + 0.5)) * s; };
        apply_size(m_drag_size.cx + step(now.x - m_drag_origin.x, GenFrame::step_w),
                   m_drag_size.cy + step(now.y - m_drag_origin.y, GenFrame::step_h));
        break;
    }
    default: break;
    }
}

void ArtWindow::on_mouse_up(POINT p) {
    const Part part = m_pressed;
    m_pressed = Part::none;
    if (part == Part::close && hit(p) == Part::close) show(false);
    if (part == Part::resize) save_position();
    invalidate();
}

void ArtWindow::on_mouse_leave_capture() {
    m_pressed = Part::none;
    invalidate();
}

void ArtWindow::apply_size(int w, int h) {
    w = std::clamp(w, GenFrame::min_width, max_size);
    h = std::clamp(h, GenFrame::min_height, max_size);
    if (w == m_w && h == m_h) return;
    m_w = w;
    m_h = h;
    config::art_width = w;
    config::art_height = h;
    apply_geometry();
}

void ArtWindow::apply_resize_mode() {
    if (config::smooth_resize.get()) return;
    apply_size(m_w - m_w % GenFrame::step_w, m_h - (m_h - GenFrame::min_height) % GenFrame::step_h);
}

bool ArtWindow::on_message(UINT msg, WPARAM, LPARAM, LRESULT&) {
    if (msg == WM_SIZE) layout_element();
    return false;
}

void ArtWindow::ensure_element() {
    if (m_elem.is_valid() || !m_wnd) return;
    ui_element::ptr elem = find_album_art_element();
    if (elem.is_empty()) return;
    ui_element_config::ptr cfg;
    auto saved = config::art_element.get();
    if (saved.is_valid() && saved->size() > 0) cfg = ui_element_config::g_create(elem->get_guid(), saved->get_ptr(), saved->size());
    else cfg = elem->get_default_configuration();
    try {
        m_elem = elem->instantiate(m_wnd, cfg, ui_element_instance_callback_get_ptr());
    } catch (const std::exception& e) {
        FB2K_console_formatter() << "foo_winamp_skins: Album Art element failed: " << e.what();
        m_elem.release();
        return;
    }
    layout_element();
    ShowWindow(m_elem->get_wnd(), SW_SHOWNA);
}

void ArtWindow::layout_element() {
    if (m_elem.is_empty()) return;
    const RECT r = content_rect();
    SetWindowPos(m_elem->get_wnd(), nullptr, r.left, r.top, r.right - r.left, r.bottom - r.top, SWP_NOZORDER | SWP_NOACTIVATE);
}

void ArtWindow::release_element() {
    if (m_elem.is_empty()) return;
    if (auto cfg = m_elem->get_configuration(); cfg.is_valid()) config::art_element.set(cfg->get_data(), cfg->get_data_size());
    HWND child = m_elem->get_wnd();
    m_elem.release();
    if (IsWindow(child)) DestroyWindow(child);
}

// Background like Winamp's own skinned panels: genex.bmp's item background colour (pixel 48,0),
// or the playlist background from pledit.txt when the skin has no genex.bmp.
bool ArtWindow::query_color(const GUID& what, t_ui_color& out) {
    if (what == ui_color_background && m_skin) {
        const Image& genex = m_skin->bitmap(SkinBitmap::genex);
        if (genex.width > 48 && genex.height > 0) {
            const uint32_t p = genex.pixels[48];
            out = RGB((p >> 16) & 0xFF, (p >> 8) & 0xFF, p & 0xFF);
        } else {
            out = m_skin->playlist_colors().normal_bg;
        }
        return true;
    }
    if (auto m = ui_config_manager::tryGet(); m.is_valid()) return m->query_color(what, out);
    return false;
}

t_ui_font ArtWindow::query_font_ex(const GUID& what) {
    if (auto m = ui_config_manager::tryGet(); m.is_valid()) return m->query_font(what);
    return nullptr;
}

bool ArtWindow::request_activation(service_ptr_t<ui_element_instance>) {
    show(true);
    return true;
}

} // namespace wsz
