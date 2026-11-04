#include "stdafx.h"
#include "element_window.h"
#include "config.h"
#include "main_window.h"
#include "resource.h"

#include <foobar2000/SDK/coreDarkMode.h>

#include <cmath>

namespace wsz {
namespace {

constexpr int max_size = 4000;

} // namespace

std::vector<ElementInfo> list_elements() {
    std::vector<ElementInfo> out;
    for (auto e : ui_element::enumerate()) {
        if (!e->is_user_addable() || e->get_subclass() == ui_element_subclass_containers) continue;
        ElementInfo info{ e->get_guid(), {}, {} };
        e->get_name(info.name);
        if (!e->get_element_group(info.group)) info.group = "Other";
        out.push_back(std::move(info));
    }
    std::sort(out.begin(), out.end(), [](const ElementInfo& a, const ElementInfo& b) {
        if (int g = pfc::stricmp_ascii(a.group, b.group)) return g < 0;
        return pfc::stricmp_ascii(a.name, b.name) < 0;
    });
    return out;
}

ElementWindow::ElementWindow(const State& state) : m_state(state) {
    m_state.w = std::clamp(m_state.w, GenFrame::min_width, max_size);
    m_state.h = std::clamp(m_state.h, GenFrame::min_height, max_size);
    if (!config::smooth_resize.get()) {
        m_state.w = std::max(GenFrame::min_width, m_state.w - m_state.w % GenFrame::step_w);
        m_state.h = std::max(GenFrame::min_height, m_state.h - m_state.h % GenFrame::step_h);
    }
    if (!ui_element::g_get_name(m_title, m_state.guid)) m_title = "Element";
}

ElementWindow::~ElementWindow() {
    release_element();
    ui_element_instance_callback_release();
}

ElementWindow::State ElementWindow::state() {
    capture_config();
    RECT rc;
    if (m_wnd && GetWindowRect(m_wnd, &rc)) { m_state.x = rc.left; m_state.y = rc.top; }
    m_state.visible = visible();
    return m_state;
}

void ElementWindow::on_position_saved(int x, int y) {
    m_state.x = x;
    m_state.y = y;
}

void ElementWindow::on_visibility_changed(bool visible) {
    m_state.visible = visible;
    if (visible) ensure_element();
    if (MainWindow* w = MainWindow::instance()) w->invalidate();
}

void ElementWindow::on_skin_changed() {
    layout_element();
    if (m_elem.is_valid()) m_elem->notify(ui_element_notify_colors_changed, 0, nullptr, 0);
    invalidate();
}

RECT ElementWindow::content_rect() const {
    const GenFrame::Insets in = m_skin ? GenFrame::insets(*m_skin) : GenFrame::Insets{ 0, 0, 0, 0 };
    const int w = m_state.w, h = m_state.h;
    return { in.left, in.top, std::max<int>(in.left, w - in.right), std::max<int>(in.top, h - in.bottom) };
}

void ElementWindow::render(Canvas& c) {
    GenFrame::render(c, *m_skin, m_state.w, m_state.h, title(), title_lit(), m_pressed == Part::close && m_inside);
}

const char* ElementWindow::title() const {
    if (!m_state.title.is_empty()) return m_state.title;
    if (!m_label.is_empty()) return m_label;
    return m_title;
}

t_size ElementWindow::host_notify(ui_element_instance*, const GUID& what, t_size, const void* param2, t_size param2size) {
    if (what != ui_element_host_notify_set_elem_label) return 0;
    m_label.set_string(static_cast<const char*>(param2), param2size);
    invalidate();
    return 1;
}

void ElementWindow::rename() {
    struct Ctx { pfc::string8 text; fb2k::CCoreDarkModeHooks dark; } ctx;
    ctx.text = title();
    auto proc = [](HWND dlg, UINT msg, WPARAM wp, LPARAM lp) -> INT_PTR {
        auto* c = reinterpret_cast<Ctx*>(GetWindowLongPtrW(dlg, DWLP_USER));
        switch (msg) {
        case WM_INITDIALOG:
            SetWindowLongPtrW(dlg, DWLP_USER, lp);
            c = reinterpret_cast<Ctx*>(lp);
            c->dark.AddDialogWithControls(dlg);
            uSetDlgItemText(dlg, IDC_RENAME_EDIT, c->text);
            SendDlgItemMessageW(dlg, IDC_RENAME_EDIT, EM_SETSEL, 0, -1);
            return TRUE;
        case WM_COMMAND:
            if (LOWORD(wp) == IDOK) { c->text = uGetDlgItemText(dlg, IDC_RENAME_EDIT); EndDialog(dlg, IDOK); return TRUE; }
            if (LOWORD(wp) == IDCANCEL) { EndDialog(dlg, IDCANCEL); return TRUE; }
            break;
        }
        return FALSE;
    };
    if (DialogBoxParamW(core_api::get_my_instance(), MAKEINTRESOURCEW(IDD_RENAME), m_wnd, proc, reinterpret_cast<LPARAM>(&ctx)) != IDOK) return;
    // The default title (label or name) typed back means "no custom title".
    const char* fallback = !m_label.is_empty() ? m_label.get_ptr() : m_title.get_ptr();
    if (ctx.text.is_empty() || strcmp(ctx.text, fallback) == 0) m_state.title.reset();
    else m_state.title = ctx.text;
    invalidate();
    if (MainWindow* w = MainWindow::instance()) w->save_element_windows();
}

ElementWindow::Part ElementWindow::hit(POINT p) const {
    const int w = m_state.w, h = m_state.h;
    if (in_rect(p, w - 11, 3, w - 3, 12)) return Part::close;
    if (p.x >= w - 20 && p.y >= h - 20) return Part::resize;
    return Part::none;
}

// Same map Winamp uses for its generic windows (playlist cursors).
SkinCursor ElementWindow::cursor_at(POINT p) const {
    const int w = m_state.w, h = m_state.h;
    if (in_rect(p, w - 11, 3, w - 3, 12)) return SkinCursor::pl_close;
    if (in_rect(p, 0, 0, w - 1, 13)) return SkinCursor::pl_titlebar;
    if (in_rect(p, w - 20, h - 20, w - 1, h - 1)) return SkinCursor::pl_size;
    return SkinCursor::pl_normal;
}

void ElementWindow::on_mouse_down(POINT p, bool) {
    m_pressed = hit(p);
    m_inside = true;
    if (m_pressed == Part::resize) {
        GetCursorPos(&m_drag_origin);
        m_drag_size = { m_state.w, m_state.h };
    }
    invalidate();
}

void ElementWindow::on_mouse_move(POINT p) {
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

void ElementWindow::on_mouse_up(POINT p) {
    const Part part = m_pressed;
    m_pressed = Part::none;
    if (part == Part::close && hit(p) == Part::close) show(false);
    invalidate();
}

void ElementWindow::on_mouse_leave_capture() {
    m_pressed = Part::none;
    invalidate();
}

void ElementWindow::apply_size(int w, int h) {
    w = std::clamp(w, GenFrame::min_width, max_size);
    h = std::clamp(h, GenFrame::min_height, max_size);
    if (w == m_state.w && h == m_state.h) return;
    m_state.w = w;
    m_state.h = h;
    apply_geometry(false);
}

void ElementWindow::apply_resize_mode() {
    if (config::smooth_resize.get()) return;
    apply_size(std::max(GenFrame::min_width, m_state.w - m_state.w % GenFrame::step_w),
               std::max(GenFrame::min_height, m_state.h - m_state.h % GenFrame::step_h));
}

bool ElementWindow::on_message(UINT msg, WPARAM, LPARAM, LRESULT&) {
    if (msg == WM_SIZE) layout_element();
    return false;
}

void ElementWindow::ensure_element() {
    if (m_elem.is_valid() || !m_wnd) return;
    ui_element::ptr elem;
    if (!ui_element::g_find(elem, m_state.guid)) {
        FB2K_console_formatter() << "foo_winamp_skins: UI element " << pfc::print_guid(m_state.guid) << " is not available";
        return;
    }
    ui_element_config::ptr cfg = m_state.config.empty()
        ? elem->get_default_configuration()
        : ui_element_config::g_create(m_state.guid, m_state.config.data(), m_state.config.size());
    try {
        m_elem = elem->instantiate(m_wnd, cfg, ui_element_instance_callback_get_ptr());
    } catch (const std::exception& e) {
        FB2K_console_formatter() << "foo_winamp_skins: " << m_title << " failed: " << e.what();
        m_elem.release();
        return;
    }
    layout_element();
    ShowWindow(m_elem->get_wnd(), SW_SHOWNA);
}

void ElementWindow::layout_element() {
    if (m_elem.is_empty()) return;
    const RECT r = content_rect();
    SetWindowPos(m_elem->get_wnd(), nullptr, r.left, r.top, r.right - r.left, r.bottom - r.top, SWP_NOZORDER | SWP_NOACTIVATE);
}

void ElementWindow::capture_config() {
    if (m_elem.is_empty()) return;
    if (auto cfg = m_elem->get_configuration(); cfg.is_valid()) {
        const auto* p = static_cast<const uint8_t*>(cfg->get_data());
        m_state.config.assign(p, p + cfg->get_data_size());
    }
}

void ElementWindow::release_element() {
    if (m_elem.is_empty()) return;
    capture_config();
    HWND child = m_elem->get_wnd();
    m_elem.release();
    if (IsWindow(child)) DestroyWindow(child);
}

// Background like Winamp's own skinned panels: genex.bmp's item background colour (pixel 48,0), or the
// playlist background from pledit.txt when the skin has no genex.bmp. Text and selection colours come
// from pledit.txt, so the hosted element matches the playlist editor.
bool ElementWindow::query_color(const GUID& what, t_ui_color& out) {
    if (m_skin) {
        const PlaylistColors& col = m_skin->playlist_colors();
        if (what == ui_color_background) {
            const Image& genex = m_skin->bitmap(SkinBitmap::genex);
            if (genex.width > 48 && genex.height > 0) {
                const uint32_t p = genex.pixels[48];
                out = RGB((p >> 16) & 0xFF, (p >> 8) & 0xFF, p & 0xFF);
            } else {
                out = col.normal_bg;
            }
            return true;
        }
        if (what == ui_color_text) { out = col.normal; return true; }
        if (what == ui_color_highlight) { out = col.current; return true; }
        if (what == ui_color_selection) { out = col.selected_bg; return true; }
    }
    if (auto m = ui_config_manager::tryGet(); m.is_valid()) return m->query_color(what, out);
    return false;
}

t_ui_font ElementWindow::query_font_ex(const GUID& what) {
    if (auto m = ui_config_manager::tryGet(); m.is_valid()) return m->query_font(what);
    return nullptr;
}

bool ElementWindow::request_activation(service_ptr_t<ui_element_instance>) {
    show(true);
    return true;
}

} // namespace wsz
