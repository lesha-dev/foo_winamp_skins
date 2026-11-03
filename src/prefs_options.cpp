#include "stdafx.h"
#include "balance_dsp.h"
#include "config.h"
#include "guids.h"
#include "main_window.h"
#include "resource.h"

#include <foobar2000/SDK/coreDarkMode.h>

// Display > Winamp Skins > Options: general behaviour and display options.
namespace wsz {
namespace {

constexpr int default_snap = 25;

class OptionsPrefsInstance : public preferences_page_instance {
public:
    explicit OptionsPrefsInstance(preferences_page_callback::ptr callback) : m_callback(callback) {}

    void create(HWND parent) {
        CreateDialogParamW(core_api::get_my_instance(), MAKEINTRESOURCEW(IDD_PREFS_OPTIONS), parent, &s_dlgproc, reinterpret_cast<LPARAM>(this));
    }

    t_uint32 get_state() override {
        t_uint32 s = preferences_state::resettable | preferences_state::dark_mode_supported;
        if (has_changed()) s |= preferences_state::changed;
        return s;
    }
    fb2k::hwnd_t get_wnd() override { return m_wnd; }

    void apply() override {
        config::winamp_hotkeys = checked(IDC_WINAMP_HOTKEYS);
        balance::set_enabled(checked(IDC_BALANCE_DSP));
        config::balance_snap = snap_value();
        config::dim_titlebars = checked(IDC_DIM_TITLEBARS);
        if (MainWindow* w = MainWindow::instance()) w->invalidate_all();
        changed();
    }

    void reset() override {
        CheckDlgButton(m_wnd, IDC_WINAMP_HOTKEYS, BST_CHECKED);
        CheckDlgButton(m_wnd, IDC_BALANCE_DSP, BST_CHECKED);
        SetDlgItemInt(m_wnd, IDC_BALANCE_SNAP, default_snap, FALSE);
        CheckDlgButton(m_wnd, IDC_DIM_TITLEBARS, BST_CHECKED);
        changed();
    }

private:
    static INT_PTR CALLBACK s_dlgproc(HWND wnd, UINT msg, WPARAM wp, LPARAM lp) {
        if (msg == WM_INITDIALOG) {
            reinterpret_cast<OptionsPrefsInstance*>(lp)->m_wnd = wnd;
            SetWindowLongPtrW(wnd, DWLP_USER, lp);
        }
        auto* self = reinterpret_cast<OptionsPrefsInstance*>(GetWindowLongPtrW(wnd, DWLP_USER));
        if (!self) return FALSE;
        service_ptr_t<OptionsPrefsInstance> keep(self);
        switch (msg) {
        case WM_INITDIALOG:
            self->on_init();
            return TRUE;
        case WM_COMMAND:
            if (HIWORD(wp) == BN_CLICKED || (LOWORD(wp) == IDC_BALANCE_SNAP && HIWORD(wp) == EN_CHANGE && self->m_initialized))
                self->changed();
            return TRUE;
        case WM_DESTROY:
            SetWindowLongPtrW(wnd, DWLP_USER, 0);
            self->m_wnd = nullptr;
            return FALSE;
        }
        return FALSE;
    }

    void on_init() {
        m_dark.AddDialogWithControls(m_wnd);
        CheckDlgButton(m_wnd, IDC_WINAMP_HOTKEYS, config::winamp_hotkeys.get() ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(m_wnd, IDC_BALANCE_DSP, balance::enabled() ? BST_CHECKED : BST_UNCHECKED);
        SetDlgItemInt(m_wnd, IDC_BALANCE_SNAP, static_cast<UINT>(config::balance_snap.get()), FALSE);
        SendDlgItemMessageW(m_wnd, IDC_BALANCE_SNAP, EM_SETLIMITTEXT, 2, 0);
        CheckDlgButton(m_wnd, IDC_DIM_TITLEBARS, config::dim_titlebars.get() ? BST_CHECKED : BST_UNCHECKED);
        m_initialized = true;
    }

    bool checked(int id) const { return IsDlgButtonChecked(m_wnd, id) == BST_CHECKED; }

    int snap_value() const {
        BOOL ok = FALSE;
        const UINT v = GetDlgItemInt(m_wnd, IDC_BALANCE_SNAP, &ok, FALSE);
        return ok ? static_cast<int>(std::min(v, 99u)) : default_snap;
    }

    bool has_changed() const {
        if (!m_wnd) return false;
        return checked(IDC_WINAMP_HOTKEYS) != config::winamp_hotkeys.get()
            || checked(IDC_BALANCE_DSP) != balance::enabled()
            || snap_value() != static_cast<int>(config::balance_snap.get())
            || checked(IDC_DIM_TITLEBARS) != config::dim_titlebars.get();
    }

    void changed() { m_callback->on_state_changed(); }

    HWND m_wnd = nullptr;
    preferences_page_callback::ptr m_callback;
    fb2k::CCoreDarkModeHooks m_dark;
    bool m_initialized = false;
};

class OptionsPrefsPage : public preferences_page_v3 {
public:
    const char* get_name() override { return "Options"; }
    GUID get_guid() override { return guids::prefs_options_page; }
    GUID get_parent_guid() override { return guids::prefs_page; }
    preferences_page_instance::ptr instantiate(fb2k::hwnd_t parent, preferences_page_callback::ptr callback) override {
        auto obj = fb2k::service_new<OptionsPrefsInstance>(callback);
        obj->create(parent);
        if (!obj->get_wnd()) throw exception_win32(GetLastError());
        return obj;
    }
};

preferences_page_factory_t<OptionsPrefsPage> g_options_prefs_page;

} // namespace
} // namespace wsz
