#pragma once

#include "gen_frame.h"
#include "hosted_element.h"
#include "skin_window.h"

#include <vector>

namespace wsz {

// A Winamp generic frame (gen.bmp) hosting one of foobar2000's UI elements (Album Art Viewer, Console, ...).
class ElementWindow : public SkinWindow, public ElementPanel {
public:
    using State = ElementState;

    explicit ElementWindow(const State& state);
    ~ElementWindow() override;

    // ElementPanel
    SkinWindow& window() override { return *this; }
    const GUID& element_guid() const override { return m_state.guid; }
    ElementState element_state() override;
    void release_element() override;
    void on_skin_changed() override;
    void apply_resize_mode() override; // smooth resize turned off: snap the size to 25x29 steps

    bool can_rename() const override { return true; }
    void rename() override;

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
    bool on_message(UINT msg, WPARAM wp, LPARAM lp, LRESULT& ret) override;

private:
    enum class Part { none, close, resize };
    Part hit(POINT p) const;
    RECT content_rect() const;
    void apply_size(int w, int h);
    void layout_element();
    // Title shown: the user's, else the label the element asked for, else the element's name.
    const char* title() const;

    State m_state;
    HostedElement m_host;
    Part m_pressed = Part::none;
    bool m_inside = false;
    POINT m_drag_origin = {};
    SIZE m_drag_size = {};
};

// Elements offered in the Elements menu: user-addable, not containers (they need Default UI's edit mode).
struct ElementInfo {
    GUID guid;
    pfc::string8 name;
    pfc::string8 group;
};
std::vector<ElementInfo> list_elements();

} // namespace wsz
