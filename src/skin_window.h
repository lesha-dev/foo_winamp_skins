#pragma once

#include "canvas.h"
#include "skin.h"
#include "tooltip.h"

namespace wsz {

// Base for the secondary Winamp windows (equalizer, playlist): a borderless popup owned by the main
// window, drawn from a 1x canvas, with caption-drag, docking/snapping and keyboard forwarding.
class SkinWindow {
public:
    virtual ~SkinWindow();

    void create(HWND owner, const wchar_t* title, int x, int y);
    void destroy();
    HWND hwnd() const { return m_wnd; }

    void set_skin(const Skin* skin);
    bool visible() const { return m_wnd && IsWindowVisible(m_wnd); }
    void show(bool on);
    void invalidate();

protected:
    // Size in skin pixels and the integer display scale.
    virtual SIZE skin_size() const = 0;
    virtual int scale() const { return 1; }
    virtual void render(Canvas& c) = 0;
    // Client points over controls get mouse input; everything else drags the window.
    virtual bool is_control(POINT skin_pt) const = 0;
    virtual void on_mouse_down(POINT, bool /*dblclick*/) {}
    virtual void on_mouse_move(POINT) {}
    virtual void on_mouse_up(POINT) {}
    virtual void on_mouse_leave_capture() {}
    // While true, the window keeps the mouse captured after button-up and gets every click (open popup).
    virtual bool holds_mouse() const { return false; }
    // Tooltip text for the point (skin coordinates), or null.
    virtual const wchar_t* tooltip_at(POINT) const { return nullptr; }
    virtual void on_right_click(POINT screen_pt);
    virtual void on_wheel(int /*delta*/) {}
    virtual bool on_key(WPARAM /*vk*/) { return false; }
    virtual void on_caption_dblclick() {}
    virtual void on_position_saved(int /*x*/, int /*y*/) {}
    virtual void on_visibility_changed(bool /*visible*/) {}
    virtual const char* region_name() const { return nullptr; }
    // Return true to swallow the message.
    virtual bool on_message(UINT, WPARAM, LPARAM, LRESULT&) { return false; }

    // Recomputes window size, canvas and region from skin_size()/scale(); keeps docked windows attached.
    void apply_geometry();
    // Highlighted titlebar: focused, or always when dimming is off.
    bool title_lit() const;
    POINT to_skin(POINT client) const { return { client.x / scale(), client.y / scale() }; }

    HWND m_wnd = nullptr;
    const Skin* m_skin = nullptr;
    bool m_active = false;
    Canvas m_canvas;
    Tooltip m_tooltip;

private:
    static LRESULT CALLBACK s_wndproc(HWND, UINT, WPARAM, LPARAM);
    LRESULT wndproc(UINT msg, WPARAM wp, LPARAM lp);
    void apply_region();
    void save_window_position();
    void paint();
};

} // namespace wsz
