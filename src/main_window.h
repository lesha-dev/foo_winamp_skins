#pragma once

#include "canvas.h"
#include "controls.h"
#include "element_window.h"
#include "main_renderer.h"
#include "skin.h"
#include "tooltip.h"
#include "visualizer.h"

#include <memory>

namespace wsz {

class EqWindow;
class PlaylistWindow;

// The skinned Winamp main window. Owned by the UI module; lives on the main thread.
class MainWindow {
public:
    using HookProc = user_interface::HookProc_t;

    static constexpr int base_width = 275;
    static constexpr int base_height = 116;

    MainWindow();
    ~MainWindow();
    MainWindow(const MainWindow&) = delete;
    MainWindow& operator=(const MainWindow&) = delete;

    HWND create(HookProc hook);
    void destroy();
    HWND hwnd() const { return m_wnd; }

    // The live window, or null when this UI isn't active (e.g. preferences opened under another UI).
    static MainWindow* instance() { return s_instance; }
    void sync_always_on_top();

    bool load_skin(const char* path, bool interactive);
    void unload_skin();
    void invalidate();
    void invalidate_all(); // main + eq + playlist
    void apply_display_options(); // after the Options page changed
    void on_eq_changed();
    void set_status(const char* text);  // slider feedback in the title area; empty/null restores the title
    // The clicked window's actions (Double size, Windowshade, Rename, Close), then the common menu.
    // panel: the clicked window; null = the main window.
    void show_context_menu(POINT pt_screen, SkinWindow* panel = nullptr);
    void toggle_double_size() { set_double_size(m_scale == 1); }
    void set_vis_mode(VisMode mode);
    void set_vis_options(const VisOptions& o); // also stored in config
    // Winamp hotkeys, also used by the EQ and playlist windows. Returns true if handled.
    bool handle_key(WPARAM vk, bool alt = false);
    void toggle_eq();
    void toggle_playlist();
    void toggle_element(const GUID& guid); // shows/hides the element's window, creating it on first use
    void save_element_windows();

private:
    class Callbacks;
    friend class Callbacks;

    static LRESULT CALLBACK s_wndproc(HWND wnd, UINT msg, WPARAM wp, LPARAM lp);
    LRESULT wndproc(UINT msg, WPARAM wp, LPARAM lp);

    // painting
    void on_paint();
    void paint_placeholder(HDC dc);
    MainState build_state();
    pfc::string8 current_title();

    // input
    POINT to_skin(POINT client) const;
    SkinCursor cursor_at(POINT skin_pt) const;
    const ControlRect* hit(POINT skin_pt) const;
    void on_lbutton_down(POINT skin_pt);
    void on_mouse_move(POINT skin_pt);
    void on_lbutton_up(POINT skin_pt);
    void cancel_drag();
    void update_slider(POINT skin_pt);
    void activate_control(Control id);
    bool on_key(WPARAM vk);
    void show_vis_menu(POINT pt_screen);
    void show_order_menu(bool shuffle);
    void append_vis_items(HMENU menu);

    // visualizer
    void update_vis_timer();
    void on_vis_timer();

    // playback events
    void on_track_changed();
    void on_playback_stopped();
    void on_dynamic_info(const file_info& info);

    // window modes
    void set_shade(bool on);
    void set_double_size(bool on);
    void set_always_on_top(bool on);
    void apply_geometry();
    void apply_region();
    void save_position();
    void save_positions(); // main + eq + playlist

    // skins

    static MainWindow* s_instance;

    HWND m_wnd = nullptr;
    HookProc m_hook = nullptr;
    std::unique_ptr<Callbacks> m_callbacks;
    std::unique_ptr<Skin> m_skin;
    Canvas m_canvas;
    titleformat_object::ptr m_title_script;   // Options > Main window title
    titleformat_object::ptr m_caption_script; // taskbar caption
    void update_title_formats();
    HFONT m_title_font = nullptr, m_title_font2x = nullptr; // playlist font for the non-bitmap title
    void update_title_fonts();
    void skin_changed(); // propagate m_skin to all windows

    int m_scale = 1;
    bool m_shade = false;
    bool m_always_on_top = false;
    bool m_active = false;

    Control m_pressed = Control::none; // control under mouse capture
    bool m_pressed_inside = false;
    double m_drag_value = 0;           // slider value while dragging (0..1, balance -1..1)
    pfc::string8 m_status;             // slider feedback shown instead of the title
    int m_marquee_px = 0;              // title scroll position (px)
    int m_marquee_hold = 0;            // timer ticks before auto-scroll resumes after a drag
    int m_marquee_drag_x = 0;          // last cursor x while dragging the title
    int m_kbps = -1;
    int m_khz = -1;

    Visualizer m_vis;
    Tooltip m_tooltip;
    std::unique_ptr<EqWindow> m_eq;
    std::unique_ptr<PlaylistWindow> m_pl;
    std::vector<std::unique_ptr<ElementPanel>> m_elements; // Elements menu windows
    ElementPanel* create_element_window(ElementState s);
    ui_selection_holder::ptr m_selection; // selection viewers (Album Art, Properties) follow the playlist selection
    bool m_vis_running = false;
};

} // namespace wsz
