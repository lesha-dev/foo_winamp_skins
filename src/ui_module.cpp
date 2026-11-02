#include "stdafx.h"
#include "guids.h"
#include "main_window.h"
#include "version.h"

namespace wsz {
namespace {

class WinampUI : public user_interface_v2 {
public:
    const char* get_name() override { return COMPONENT_NAME; }
    GUID get_guid() override { return guids::ui_module; }

    fb2k::hwnd_t init(HookProc_t hook) override {
        HWND wnd = m_main.create(hook);
        ShowWindow(wnd, SW_SHOWNORMAL);
        UpdateWindow(wnd);
        return wnd;
    }

    void shutdown() override { m_main.destroy(); }

    void activate() override {
        HWND wnd = m_main.hwnd();
        if (!wnd) return;
        ShowWindow(wnd, IsIconic(wnd) ? SW_RESTORE : SW_SHOW);
        SetForegroundWindow(wnd);
    }

    void hide() override {
        if (HWND wnd = m_main.hwnd()) ShowWindow(wnd, SW_MINIMIZE);
    }

    bool is_visible() override {
        HWND wnd = m_main.hwnd();
        return wnd && IsWindowVisible(wnd) && !IsIconic(wnd);
    }

    void override_statusbar_text(const char*) override {}
    void revert_statusbar_text() override {}
    void show_now_playing() override {}

    bool query_capability(const GUID&) override { return false; }

private:
    MainWindow m_main;
};

user_interface_factory<WinampUI> g_ui_factory;

} // namespace
} // namespace wsz
