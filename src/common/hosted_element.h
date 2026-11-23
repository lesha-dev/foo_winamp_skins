#pragma once

#include "common/cui_panels.h"

#include <functional>
#include <optional>
#include <vector>

namespace wsz {

// Colours a host gives its elements instead of foobar2000's (e.g. a classic skin's playlist colours).
struct HostColors {
    COLORREF background, text, highlight, selection;
};

// One of foobar2000's UI elements living inside a skinned window. Colours come from the host (see
// set_colors) or foobar2000, fonts from foobar2000. The GUID may also name a Columns UI panel, which keeps
// Columns UI's colours and fonts.
class HostedElement : private ui_element_instance_callback_receiver {
public:
    HostedElement(const GUID& guid, std::vector<uint8_t> config);
    ~HostedElement();
    HostedElement(const HostedElement&) = delete;
    HostedElement& operator=(const HostedElement&) = delete;

    const GUID& guid() const { return m_guid; }
    const pfc::string8& name() const { return m_name; }
    const pfc::string8& label() const { return m_label; } // set by the element; may be empty

    // Instantiates the element as a child of `parent` (once). Returns false if it isn't available.
    bool create(HWND parent);
    HWND hwnd() const { return m_elem.is_valid() ? m_elem->get_wnd() : m_panel ? m_panel->hwnd() : nullptr; }
    bool is_cui() const { return m_panel != nullptr; }
    void set_rect(const RECT& r);
    // Host colours; nullopt = foobar2000's.
    void set_colors(const std::optional<HostColors>& colors);
    // Sets the host's cursor for the element's client area (instead of the system arrow); empty = arrow.
    std::function<void()> on_set_cursor;
    void set_default_cursor() const;
    // Font for list/playlist text instead of foobar2000's (null = foobar2000's); the caller keeps it alive.
    void set_list_font(HFONT font);
    // The element's configuration (refreshed from the live element).
    const std::vector<uint8_t>& config();
    // Saves the configuration and destroys the element.
    void release();

    std::function<void()> on_label_changed;
    std::function<void()> on_activate;

private:
    bool query_color(const GUID& what, t_ui_color& out) override;
    t_ui_font query_font_ex(const GUID& what) override;
    bool request_activation(service_ptr_t<ui_element_instance>) override;
    t_size host_notify(ui_element_instance* source, const GUID& what, t_size param1, const void* param2, t_size param2size) override;

    GUID m_guid;
    std::vector<uint8_t> m_config;
    pfc::string8 m_name, m_label;
    std::optional<HostColors> m_colors;
    HFONT m_list_font = nullptr;
    ui_element_instance::ptr m_elem;
    std::unique_ptr<cui::Panel> m_panel; // set when the GUID is a Columns UI panel
};

// Display name of a Default UI element or Columns UI panel.
bool element_name(const GUID& guid, pfc::string8& out);

// Default UI elements found by name (no private GUIDs assumed); guid_null if missing.
GUID find_element(const char* name);
inline GUID playlist_view_guid() { return find_element("Playlist View"); }

// A hosted element's scrollable list: the first child window of `parent` with a vertical scrollbar.
HWND find_vscroll_window(HWND parent);
// Its vertical scroll state; false without a window or a valid range.
bool get_vscroll(HWND list, SCROLLINFO& si);
// Scrolls it to a position (as if its thumb were dragged there).
void set_vscroll_pos(HWND list, int pos);

// Elements offered in the Elements menu: user-addable, not containers (they need Default UI's edit mode).
struct ElementInfo {
    GUID guid;
    pfc::string8 name;
    pfc::string8 group;
    bool cui = false;      // a Columns UI panel
    bool playlist = false; // a playlist view: shown in the playlist editor
};
std::vector<ElementInfo> list_elements();
// Columns UI panels offered in their own menu (none without Columns UI). Panels named like one of
// `elements` (a component registering both kinds) are left out; playlist views are always offered.
std::vector<ElementInfo> list_cui_panels(const std::vector<ElementInfo>& elements);

} // namespace wsz
