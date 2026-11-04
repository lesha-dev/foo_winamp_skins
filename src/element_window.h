#pragma once

#include "gen_frame.h"
#include "skin_window.h"

#include <vector>

namespace wsz {

// A Winamp generic frame (gen.bmp) hosting one of foobar2000's UI elements (Album Art Viewer, Console, ...).
class ElementWindow : public SkinWindow, private ui_element_instance_callback_receiver {
public:
    // Remembered per element: placement and the element's own configuration.
    struct State {
        GUID guid = {};
        bool visible = false;
        int x = CW_USEDEFAULT, y = CW_USEDEFAULT;
        int w = GenFrame::full_width, h = GenFrame::full_height + 4 * GenFrame::step_h;
        std::vector<uint8_t> config;
        pfc::string8 title; // user's title; empty = the element's label or name
    };

    explicit ElementWindow(const State& state);
    ~ElementWindow() override;

    const GUID& guid() const { return m_state.guid; }
    // Current placement and element configuration (refreshed from the live element).
    State state();
    void on_skin_changed();
    void apply_resize_mode(); // smooth resize turned off: snap the size to 25x29 steps
    // Saves the element's settings and destroys it (before the window goes away).
    void release_element();

protected:
    SIZE skin_size() const override { return { m_state.w, m_state.h }; }
    void render(Canvas& c) override;
    bool is_control(POINT p) const override { return hit(p) != Part::none; }
    void on_mouse_down(POINT p, bool dbl) override;
    void on_mouse_move(POINT p) override;
    void on_mouse_up(POINT p) override;
    void on_mouse_leave_capture() override;
    SkinCursor cursor_at(POINT p) const override;
    void on_position_saved(int x, int y) override;
    void on_visibility_changed(bool visible) override;
    bool can_rename() const override { return true; }
    void rename() override;
    bool on_message(UINT msg, WPARAM wp, LPARAM lp, LRESULT& ret) override;

private:
    enum class Part { none, close, resize };
    Part hit(POINT p) const;
    RECT content_rect() const;
    void apply_size(int w, int h);
    void ensure_element();
    void layout_element();
    void capture_config();

    // ui_element_instance_callback_receiver: colours from the skin's playlist, fonts from foobar2000.
    bool query_color(const GUID& what, t_ui_color& out) override;
    t_ui_font query_font_ex(const GUID& what) override;
    bool request_activation(service_ptr_t<ui_element_instance>) override;
    t_size host_notify(ui_element_instance* source, const GUID& what, t_size param1, const void* param2, t_size param2size) override;

    // Title shown: the user's, else the label the element asked for, else the element's name.
    const char* title() const;

    State m_state;
    pfc::string8 m_title; // element's name
    pfc::string8 m_label; // element-provided label
    Part m_pressed = Part::none;
    bool m_inside = false;
    POINT m_drag_origin = {};
    SIZE m_drag_size = {};
    ui_element_instance::ptr m_elem;
};

// Elements offered in the Elements menu: user-addable, not containers (they need Default UI's edit mode).
struct ElementInfo {
    GUID guid;
    pfc::string8 name;
    pfc::string8 group;
};
std::vector<ElementInfo> list_elements();

} // namespace wsz
