#include "stdafx.h"
#include "hosted_element.h"
#include "config.h"

#include <algorithm>
#include <memory>

namespace wsz {

namespace {

// Elements alive on the UI thread. While any exists, a thread hook replaces the system arrow that a
// hosted element's windows set in WM_SETCURSOR with the skin's playlist cursor; other cursors (I-beam,
// hand, splitters...) are left alone.
std::vector<HostedElement*> g_live;
HHOOK g_cursor_hook = nullptr;

LRESULT CALLBACK cursor_hook(int code, WPARAM wp, LPARAM lp) {
    if (code == HC_ACTION) {
        const auto* m = reinterpret_cast<const CWPRETSTRUCT*>(lp);
        // Only the original target's return: by then its parents have had their say too.
        if (m->message == WM_SETCURSOR && reinterpret_cast<HWND>(m->wParam) == m->hwnd
            && config::skin_cursors.get() && GetCursor() == LoadCursorW(nullptr, IDC_ARROW)) {
            for (HostedElement* e : g_live) {
                const HWND root = e->hwnd();
                if (root && (root == m->hwnd || IsChild(root, m->hwnd))) { e->set_default_cursor(); break; }
            }
        }
    }
    return CallNextHookEx(g_cursor_hook, code, wp, lp);
}

// Keys pressed inside a hosted element run foobar2000's keyboard shortcuts, like in Default UI (typing in
// edit boxes excluded by the SDK helper). Keys on our own windows are handled in their window procedures.
class KeyFilter : public message_filter_impl_base {
public:
    bool pretranslate_message(MSG* m) override {
        if (m->message != WM_KEYDOWN && m->message != WM_SYSKEYDOWN) return false;
        for (HostedElement* e : g_live) {
            const HWND root = e->hwnd();
            if (root && (root == m->hwnd || IsChild(root, m->hwnd)))
                return keyboard_shortcut_manager_v2::get()->pretranslate_message(m, GetAncestor(m->hwnd, GA_ROOT));
        }
        return false;
    }
};
std::unique_ptr<KeyFilter> g_key_filter;

} // namespace

GUID find_element(const char* wanted) {
    for (auto e : ui_element::enumerate()) {
        pfc::string8 name;
        e->get_name(name);
        if (strcmp(name, wanted) == 0) return e->get_guid();
    }
    return pfc::guid_null;
}

bool element_name(const GUID& guid, pfc::string8& out) {
    if (ui_element::g_get_name(out, guid)) return true;
    cui::PanelInfo info;
    if (!cui::find_panel(guid, &info)) return false;
    out = info.name;
    return true;
}

HostedElement::HostedElement(const GUID& guid, std::vector<uint8_t> config) : m_guid(guid), m_config(std::move(config)) {
    ui_element::ptr elem;
    if (!ui_element::g_find(elem, m_guid) && cui::find_panel(m_guid)) m_panel = std::make_unique<cui::Panel>(m_guid);
    if (!element_name(m_guid, m_name)) m_name = "Element";
    if (g_live.empty() && !g_cursor_hook) g_cursor_hook = SetWindowsHookExW(WH_CALLWNDPROCRET, cursor_hook, nullptr, GetCurrentThreadId());
    if (!g_key_filter) g_key_filter = std::make_unique<KeyFilter>();
    g_live.push_back(this);
}

HostedElement::~HostedElement() {
    release();
    ui_element_instance_callback_release();
    g_live.erase(std::remove(g_live.begin(), g_live.end(), this), g_live.end());
    if (g_live.empty() && g_cursor_hook) { UnhookWindowsHookEx(g_cursor_hook); g_cursor_hook = nullptr; }
    if (g_live.empty()) g_key_filter.reset();
}

void HostedElement::set_default_cursor() const {
    if (m_skin) set_skin_cursor(m_skin, SkinCursor::pl_normal);
}

bool HostedElement::create(HWND parent) {
    if (m_panel) return m_panel->create(parent, m_config);
    if (m_elem.is_valid()) return true;
    ui_element::ptr elem;
    if (!ui_element::g_find(elem, m_guid)) {
        FB2K_console_formatter() << "foo_winamp_skins: UI element " << pfc::print_guid(m_guid) << " is not available";
        return false;
    }
    ui_element_config::ptr cfg = m_config.empty()
        ? elem->get_default_configuration()
        : ui_element_config::g_create(m_guid, m_config.data(), m_config.size());
    try {
        m_elem = elem->instantiate(parent, cfg, ui_element_instance_callback_get_ptr());
    } catch (const std::exception& e) {
        FB2K_console_formatter() << "foo_winamp_skins: " << m_name << " failed: " << e.what();
        m_elem.release();
        return false;
    }
    ShowWindow(m_elem->get_wnd(), SW_SHOWNA);
    return true;
}

void HostedElement::set_rect(const RECT& r) {
    if (HWND w = hwnd()) SetWindowPos(w, nullptr, r.left, r.top, r.right - r.left, r.bottom - r.top, SWP_NOZORDER | SWP_NOACTIVATE);
}

// Background like Winamp's own skinned panels: genex.bmp's item background colour (pixel 48,0), or the
// playlist background from pledit.txt when the skin has no genex.bmp.
static COLORREF skin_background(const Skin& skin) {
    const Image& genex = skin.bitmap(SkinBitmap::genex);
    if (genex.width > 48 && genex.height > 0) {
        const uint32_t p = genex.pixels[48];
        return RGB((p >> 16) & 0xFF, (p >> 8) & 0xFF, p & 0xFF);
    }
    return skin.playlist_colors().normal_bg;
}

void HostedElement::set_skin(const Skin* skin) {
    m_skin = skin;
    if (m_panel) {
        if (m_skin) {
            const PlaylistColors& col = m_skin->playlist_colors();
            // Winamp keeps the text colour on selected rows.
            const cui::ListColours colours{col.normal, skin_background(*m_skin), col.normal, col.selected_bg};
            m_panel->set_list_colours(&colours);
        } else {
            m_panel->set_list_colours(nullptr);
        }
    }
    if (m_elem.is_valid()) m_elem->notify(ui_element_notify_colors_changed, 0, nullptr, 0);
}

const std::vector<uint8_t>& HostedElement::config() {
    if (m_panel) m_panel->config(m_config);
    if (m_elem.is_valid()) {
        if (auto cfg = m_elem->get_configuration(); cfg.is_valid()) {
            const auto* p = static_cast<const uint8_t*>(cfg->get_data());
            m_config.assign(p, p + cfg->get_data_size());
        }
    }
    return m_config;
}

void HostedElement::release() {
    if (m_panel && m_panel->hwnd()) {
        config();
        m_panel->release();
    }
    if (m_elem.is_empty()) return;
    config();
    HWND child = m_elem->get_wnd();
    m_elem.release();
    if (IsWindow(child)) DestroyWindow(child);
}

// Text and selection colours come from pledit.txt, so hosted elements match the playlist editor.
bool HostedElement::query_color(const GUID& what, t_ui_color& out) {
    if (m_skin) {
        const PlaylistColors& col = m_skin->playlist_colors();
        if (what == ui_color_background) { out = skin_background(*m_skin); return true; }
        if (what == ui_color_text) { out = col.normal; return true; }
        if (what == ui_color_highlight) { out = col.current; return true; }
        if (what == ui_color_selection) { out = col.selected_bg; return true; }
    }
    if (auto m = ui_config_manager::tryGet(); m.is_valid()) return m->query_color(what, out);
    return false;
}

void HostedElement::set_list_font(HFONT font) {
    if (font == m_list_font) return;
    m_list_font = font;
    if (m_panel) m_panel->set_list_font(font);
    if (m_elem.is_valid()) m_elem->notify(ui_element_notify_font_changed, 0, nullptr, 0);
}

t_ui_font HostedElement::query_font_ex(const GUID& what) {
    if (m_list_font && (what == ui_font_playlists || what == ui_font_lists)) return m_list_font;
    if (auto m = ui_config_manager::tryGet(); m.is_valid()) return m->query_font(what);
    return nullptr;
}

bool HostedElement::request_activation(service_ptr_t<ui_element_instance>) {
    if (on_activate) on_activate();
    return true;
}

t_size HostedElement::host_notify(ui_element_instance*, const GUID& what, t_size, const void* param2, t_size param2size) {
    if (what != ui_element_host_notify_set_elem_label) return 0;
    m_label.set_string(static_cast<const char*>(param2), param2size);
    if (on_label_changed) on_label_changed();
    return 1;
}

} // namespace wsz
