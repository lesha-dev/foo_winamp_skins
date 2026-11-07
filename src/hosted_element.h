#pragma once

#include "gen_frame.h"
#include "skin.h"

#include <functional>
#include <vector>

namespace wsz {

class SkinWindow;

// Remembered per element window: placement, the element's own configuration and window options.
struct ElementState {
    GUID guid = {};
    bool visible = false;
    bool shade = false;
    bool double_size = false;
    int x = CW_USEDEFAULT, y = CW_USEDEFAULT;
    int w = GenFrame::full_width, h = GenFrame::full_height + 4 * GenFrame::step_h;
    std::vector<uint8_t> config;
    pfc::string8 title; // user's title; empty = the element's label or name
};

// One of foobar2000's UI elements living inside a skinned window. Colours come from the skin's playlist
// (background from genex.bmp when present), fonts from foobar2000.
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
    HWND hwnd() const { return m_elem.is_valid() ? m_elem->get_wnd() : nullptr; }
    void set_rect(const RECT& r);
    void set_skin(const Skin* skin);
    // Shows the skin's cursor for the element's client area (instead of the system arrow).
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
    const Skin* m_skin = nullptr;
    HFONT m_list_font = nullptr;
    ui_element_instance::ptr m_elem;
};

// Default UI elements found by name (no private GUIDs assumed); guid_null if missing.
GUID find_element(const char* name);
inline GUID playlist_view_guid() { return find_element("Playlist View"); }

// A window that hosts an element and is listed in the Elements menu (generic frame or playlist editor).
class ElementPanel {
public:
    virtual ~ElementPanel() = default;
    virtual SkinWindow& window() = 0;
    virtual const GUID& element_guid() const = 0;
    virtual ElementState element_state() = 0;
    virtual void release_element() = 0;
    virtual void on_skin_changed() = 0;
    virtual void apply_resize_mode() = 0;
};

} // namespace wsz
