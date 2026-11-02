#pragma once

namespace wsz {

// The skinned Winamp main window. Owned by the UI module; lives on the main thread.
class MainWindow {
public:
    using HookProc = user_interface::HookProc_t;

    static constexpr int base_width = 275;
    static constexpr int base_height = 116;

    HWND create(HookProc hook);
    void destroy();
    HWND hwnd() const { return m_wnd; }

private:
    static LRESULT CALLBACK s_wndproc(HWND wnd, UINT msg, WPARAM wp, LPARAM lp);
    LRESULT wndproc(UINT msg, WPARAM wp, LPARAM lp);

    void on_paint();
    LRESULT on_nchittest(LPARAM lp);
    void show_context_menu(POINT pt_screen);
    void save_position();

    HWND m_wnd = nullptr;
    HookProc m_hook = nullptr;
    int m_scale = 1;
};

} // namespace wsz
