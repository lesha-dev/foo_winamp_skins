#pragma once

#include "gen_frame.h"
#include "skin_window.h"

namespace wsz {

// Album Art window: a Winamp generic frame hosting foobar2000's own Album Art UI element.
class ArtWindow : public SkinWindow, private ui_element_instance_callback_receiver {
public:
    ArtWindow();
    ~ArtWindow() override;

    void save_position();
    void on_skin_changed();
    void apply_resize_mode(); // smooth resize turned off: snap the size to 25x29 steps
    // Saves the element's settings and destroys it (before the window goes away).
    void release_element();

protected:
    SIZE skin_size() const override { return { m_w, m_h }; }
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
    void ensure_element();
    void layout_element();

    // ui_element_instance_callback_receiver: colours and fonts come from foobar2000's UI settings.
    bool query_color(const GUID& what, t_ui_color& out) override;
    t_ui_font query_font_ex(const GUID& what) override;
    bool request_activation(service_ptr_t<ui_element_instance>) override;

    int m_w = GenFrame::min_width, m_h = GenFrame::min_height + 4 * GenFrame::step_h;
    Part m_pressed = Part::none;
    bool m_inside = false;
    POINT m_drag_origin = {};
    SIZE m_drag_size = {};
    ui_element_instance::ptr m_elem;
};

} // namespace wsz
