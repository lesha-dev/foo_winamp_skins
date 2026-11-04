#pragma once

#include "skin_window.h"

namespace wsz {

class PlaylistWindow : public SkinWindow {
public:
    PlaylistWindow();
    ~PlaylistWindow() override;

    static constexpr int min_width = 275, min_height = 116, shade_height = 14;
    static constexpr int step_w = 25, step_h = 29;
    static constexpr int max_extra_w = 100 * step_w, max_extra_h = 100 * step_h;

    void set_shade(bool on);
    bool shade() const { return m_shade; }
    void save_position();
    void on_skin_changed();
    // Mini time display, fed by the main window so both always show the same value.
    void set_time(bool show, bool negative, int seconds);
    void apply_resize_mode(); // smooth resize turned off: snap the size to 25x29 steps
    void update_title_formats(); // Options > Title formatting changed

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
    void move_focus(int delta, bool extend);
    void run_menu_action(Part menu, int item);
    void playlist_changed();
    void recompute_lengths();
    void apply_size(int w_extra, int h_extra); // px beyond the minimum size
    void update_font();

    void render_normal(Canvas& c);
    void render_shade(Canvas& c);
    void render_list(Canvas& c);
    void render_menu(Canvas& c);

    std::unique_ptr<Callbacks> m_callbacks;
    titleformat_object::ptr m_title_script; // rows
    titleformat_object::ptr m_shade_script; // windowshade title
    HFONT m_font = nullptr;

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
    int m_thumb_grab = 0;         // scrollbar: cursor offset within the thumb

    bool m_time_show = false, m_time_negative = false;
    int m_time_seconds = 0;

    bool m_lengths_dirty = true;
    double m_total_len = 0, m_sel_len = 0;
};

} // namespace wsz
