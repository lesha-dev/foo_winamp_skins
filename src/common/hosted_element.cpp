#include "common/stdafx.h"
#include "common/hosted_element.h"
#include "common/config.h"

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
    if (on_set_cursor) on_set_cursor();
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

void HostedElement::set_colors(const std::optional<HostColors>& colors) {
    m_colors = colors;
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

bool HostedElement::query_color(const GUID& what, t_ui_color& out) {
    if (m_colors) {
        if (what == ui_color_background) { out = m_colors->background; return true; }
        if (what == ui_color_text) { out = m_colors->text; return true; }
        if (what == ui_color_highlight) { out = m_colors->highlight; return true; }
        if (what == ui_color_selection) { out = m_colors->selection; return true; }
    }
    if (auto m = ui_config_manager::tryGet(); m.is_valid()) return m->query_color(what, out);
    return false;
}

void HostedElement::set_list_font(HFONT font) {
    if (font == m_list_font) return;
    m_list_font = font;
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

HWND find_vscroll_window(HWND parent) {
    if (!parent) return nullptr;
    HWND found = nullptr;
    EnumChildWindows(parent, [](HWND w, LPARAM lp) -> BOOL {
        if (!(GetWindowLongPtrW(w, GWL_STYLE) & WS_VSCROLL)) return TRUE;
        *reinterpret_cast<HWND*>(lp) = w;
        return FALSE;
    }, reinterpret_cast<LPARAM>(&found));
    return found;
}

bool get_vscroll(HWND list, SCROLLINFO& si) {
    si = {};
    si.cbSize = sizeof(si);
    si.fMask = SIF_ALL;
    return list && GetScrollInfo(list, SB_VERT, &si) && si.nMax >= si.nMin;
}

void set_vscroll_pos(HWND list, int pos) {
    SCROLLINFO si{};
    si.cbSize = sizeof(si);
    si.fMask = SIF_POS;
    si.nPos = pos;
    SetScrollInfo(list, SB_VERT, &si, FALSE);
    SendMessageW(list, WM_VSCROLL, MAKEWPARAM(SB_THUMBTRACK, pos & 0xFFFF), 0);
    SendMessageW(list, WM_VSCROLL, MAKEWPARAM(SB_ENDSCROLL, 0), 0);
}

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
    const GUID playlist_view = playlist_view_guid();
    for (auto& e : out) e.playlist = e.guid == playlist_view;
    return out;
}

std::vector<ElementInfo> list_cui_panels(const std::vector<ElementInfo>& elements) {
    std::vector<ElementInfo> out;
    for (auto& p : cui::list_panels()) {
        const bool duplicate = std::any_of(elements.begin(), elements.end(),
            [&](const ElementInfo& e) { return !e.cui && pfc::stricmp_ascii(e.name, p.name) == 0; });
        if (duplicate && !p.playlist) continue;
        out.push_back({ p.guid, p.name, p.category, true, p.playlist });
    }
    return out;
}

} // namespace wsz
