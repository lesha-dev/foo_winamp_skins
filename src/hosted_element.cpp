#include "stdafx.h"
#include "hosted_element.h"

namespace wsz {

GUID find_element(const char* wanted) {
    for (auto e : ui_element::enumerate()) {
        pfc::string8 name;
        e->get_name(name);
        if (strcmp(name, wanted) == 0) return e->get_guid();
    }
    return pfc::guid_null;
}

HostedElement::HostedElement(const GUID& guid, std::vector<uint8_t> config) : m_guid(guid), m_config(std::move(config)) {
    if (!ui_element::g_get_name(m_name, m_guid)) m_name = "Element";
}

HostedElement::~HostedElement() {
    release();
    ui_element_instance_callback_release();
}

bool HostedElement::create(HWND parent) {
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

void HostedElement::set_skin(const Skin* skin) {
    m_skin = skin;
    if (m_elem.is_valid()) m_elem->notify(ui_element_notify_colors_changed, 0, nullptr, 0);
}

const std::vector<uint8_t>& HostedElement::config() {
    if (m_elem.is_valid()) {
        if (auto cfg = m_elem->get_configuration(); cfg.is_valid()) {
            const auto* p = static_cast<const uint8_t*>(cfg->get_data());
            m_config.assign(p, p + cfg->get_data_size());
        }
    }
    return m_config;
}

void HostedElement::release() {
    if (m_elem.is_empty()) return;
    config();
    HWND child = m_elem->get_wnd();
    m_elem.release();
    if (IsWindow(child)) DestroyWindow(child);
}

// Background like Winamp's own skinned panels: genex.bmp's item background colour (pixel 48,0), or the
// playlist background from pledit.txt when the skin has no genex.bmp. Text and selection colours come
// from pledit.txt, so hosted elements match the playlist editor.
bool HostedElement::query_color(const GUID& what, t_ui_color& out) {
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

} // namespace wsz
