#pragma once

#include "hosted_element.h"
#include "skin_window.h"

namespace wsz {

// Winamp's playlist editor. Its list area is our own list, or (switch in its menu) Default UI's Playlist
// View; all frame controls stay either way.
class PlaylistWindow : public SkinWindow {
public:
    PlaylistWindow();
    ~PlaylistWindow() override;

    static constexpr int min_width = 275, min_height = 116, shade_height = 14;
    static constexpr int step_w = 25, step_h = 29;
    static constexpr int max_extra_w = 100 * step_w, max_extra_h = 100 * step_h;

    void set_shade(bool on);
    bool shade() const { return m_shade; }
    bool can_shade() const override { return true; }
    bool is_shaded() const override { return m_shade; }
    void toggle_shade() override { set_shade(!m_shade); }
    void save_position();
    void on_skin_changed();
    // Mini time display, fed by the main window so both always show the same value.
    void set_time(bool show, bool negative, int seconds);
    void apply_resize_mode(); // smooth resize turned off: snap the size to 25x29 steps

    bool can_switch_view() const override { return true; }
    bool is_view_switched() const override { return m_host != nullptr; }
    void toggle_view() override { set_use_view(!m_host); }
    void set_use_view(bool on);

    // Saves the Playlist View's settings and destroys it (before the window goes away).
    void release_element();
    void update_title_formats(); // Options > Title formatting changed
    void apply_list_font();      // hosted mode: skin's font for the element, per Options
    void update_font();          // Options: playlist font / size changed

protected:
    SIZE skin_size() const override;
    void render(Canvas& c) override;
    bool is_control(POINT p) const override;
    void on_mouse_down(POINT p, bool dbl) override;
    void on_mouse_move(POINT p) override;
    void on_mouse_up(POINT p) override;
    void on_mouse_leave_capture() override;
    bool holds_mouse() const override { return m_menu_open; }
    void on_right_click(POINT screen_pt) override;
    void on_wheel(int delta) override;
    bool on_key(WPARAM vk) override;
    void on_caption_dblclick() override { set_shade(!m_shade); }
    void on_position_saved(int x, int y) override;
    void on_visibility_changed(bool visible) override;
    void on_double_size_changed() override;
    bool on_message(UINT msg, WPARAM wp, LPARAM lp, LRESULT& ret) override;

private:
    class Callbacks;
    friend class Callbacks;

    enum class Part {
        none, close, shade, list, scrollbar, resize,
        menu_add, menu_rem, menu_sel, menu_misc, menu_list,
        prev, play, pause, stop, next, eject, scroll_up, scroll_down, mini_time,
    };
    struct Rc { int x, y, w, h; bool in(POINT p) const { return p.x >= x && p.x < x + w && p.y >= y && p.y < y + h; } };

    int width() const;
    int height() const;
    Rc list_rect() const;
    int visible_rows() const;
    int row_at(POINT p) const; // item index, or -1
    Part hit(POINT p) const;
    const wchar_t* tooltip_at(POINT p) const override;
    SkinCursor cursor_at(POINT p) const override;
    void clamp_scroll();
    void ensure_visible(size_t item);
    void scroll_to_thumb(int y);
    int thumb_top() const;
    void move_focus(int delta, bool extend);
    void run_menu_action(Part menu, int item);
    void playlist_changed();
    void recompute_lengths();
    void apply_size(int w_extra, int h_extra); // px beyond the minimum size

    void render_normal(Canvas& c);
    void render_shade(Canvas& c);
    void render_list(Canvas& c);
    void render_menu(Canvas& c);

    // Playlist View mode: the element sits in a child container sized to the list area; it is made wider than the
    // container by a scrollbar's width, so its own scrollbar is hidden and ours drives its list instead.
    std::unique_ptr<HostedElement> m_host;
    HWND m_container = nullptr;
    int m_last_scroll_pos = -1;
    RECT host_rect() const; // client pixels (scaled)
    void layout_host();
    void ensure_host();
    HWND list_hwnd() const;                 // the element's scrollable list window
    bool list_scroll(SCROLLINFO& si) const;
    void update_host_clip();                // cuts an open sprite menu out of the element's area
    Part m_clip_menu = Part::none;

    std::unique_ptr<Callbacks> m_callbacks;
    titleformat_object::ptr m_title_script; // rows
    titleformat_object::ptr m_shade_script; // windowshade title
    HFONT m_font = nullptr;       // list rows, at Options' font size
    HFONT m_shade_font = nullptr; // windowshade title: same face, Winamp's fixed size
    int m_row_h = 13;             // font size + 3

    bool m_shade = false;
    int m_w_extra = 0, m_h_extra = 4 * step_h; // px beyond the minimum size
    size_t m_scroll = 0;          // first visible row
    size_t m_anchor = SIZE_MAX;   // shift-select anchor

    Part m_pressed = Part::none;
    bool m_inside = false;
    int m_menu_hover = -1;        // hovered item in an open sprite menu
    bool m_menu_open = false;     // click mode: menu stays open after release until the next click
    int menu_hover_at(POINT p) const;
    POINT m_drag_origin = {};
    SIZE m_drag_extra = {};
    int m_drag_row = -1;          // reorder drag: row under the cursor last time
    bool m_drag_reorder = false;
    size_t m_reduce_to = SIZE_MAX; // plain click on a selected row: select only it on mouse-up unless dragged
    int m_thumb_grab = 0;         // scrollbar: cursor offset within the thumb

    bool m_time_show = false, m_time_negative = false;
    int m_time_seconds = 0;

    bool m_lengths_dirty = true;
    double m_total_len = 0, m_sel_len = 0;
};

} // namespace wsz
