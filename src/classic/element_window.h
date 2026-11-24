#pragma once

#include "classic/gen_frame.h"
#include "common/hosted_element.h"
#include "classic/skin_window.h"

#include <vector>

namespace wsz {

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

// Hosted elements in classic windows: the skin's playlist colours, with the background like Winamp's own
// skinned panels (genex.bmp's item background colour, pixel 48,0, else pledit.txt's), and its cursor.
inline void apply_skin(HostedElement& e, const Skin* skin) {
    if (!skin) {
        e.set_colors(std::nullopt);
        e.on_set_cursor = nullptr;
        return;
    }
    const PlaylistColors& col = skin->playlist_colors();
    HostColors c{col.normal_bg, col.normal, col.current, col.selected_bg};
    const Image& genex = skin->bitmap(SkinBitmap::genex);
    if (genex.width > 48 && genex.height > 0) {
        const uint32_t p = genex.pixels[48];
        c.background = RGB((p >> 16) & 0xFF, (p >> 8) & 0xFF, p & 0xFF);
    }
    e.set_colors(c);
    e.on_set_cursor = [skin] { set_skin_cursor(skin, SkinCursor::pl_normal); };
}

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
    void on_double_size_changed() override;
    void on_scale_changed() override;
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

} // namespace wsz
