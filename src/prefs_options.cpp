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
constexpr int default_dock = 10;

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
        config::snap = checked(IDC_SNAP);
        config::snap_distance = dock_value();
        config::smooth_resize = checked(IDC_SMOOTH_RESIZE);
        const bool dsize_all_now = checked(IDC_DSIZE_ALL) && !config::double_size_all.get();
        config::double_size_all = checked(IDC_DSIZE_ALL);
        config::clutterbar = checked(IDC_CLUTTERBAR);
        config::oldschool_pl_buttons = checked(IDC_OLDSCHOOL_PL);
        config::tooltips = checked(IDC_TOOLTIPS);
        config::skin_cursors = checked(IDC_SKIN_CURSORS);
        config::title_easter_egg = checked(IDC_TITLE_EGG);
        config::playlist_view_skin_font = checked(IDC_PLVIEW_FONT);
        config::title_bitmap_font = checked(IDC_TITLE_BITMAP);
        config::title_font_alt = checked(IDC_TITLE_ALT);
        config::format_main = text(IDC_FORMAT_MAIN);
        config::format_playlist = text(IDC_FORMAT_PL);
        config::format_playlist_shade = text(IDC_FORMAT_PL_SHADE);
        if (MainWindow* w = MainWindow::instance()) {
            if (dsize_all_now) w->sync_double_size(); // all windows take the main window's double size
            w->apply_display_options();
        }
        changed();
    }

    void reset() override {
        CheckDlgButton(m_wnd, IDC_WINAMP_HOTKEYS, BST_CHECKED);
        CheckDlgButton(m_wnd, IDC_BALANCE_DSP, BST_CHECKED);
        SetDlgItemInt(m_wnd, IDC_BALANCE_SNAP, default_snap, FALSE);
        CheckDlgButton(m_wnd, IDC_DIM_TITLEBARS, BST_CHECKED);
        CheckDlgButton(m_wnd, IDC_SNAP, BST_CHECKED);
        SetDlgItemInt(m_wnd, IDC_SNAP_PX, default_dock, FALSE);
        CheckDlgButton(m_wnd, IDC_SMOOTH_RESIZE, BST_UNCHECKED);
        CheckRadioButton(m_wnd, IDC_DSIZE_ALL, IDC_DSIZE_EACH, IDC_DSIZE_ALL);
        CheckDlgButton(m_wnd, IDC_CLUTTERBAR, BST_CHECKED);
        CheckDlgButton(m_wnd, IDC_OLDSCHOOL_PL, BST_UNCHECKED);
        CheckDlgButton(m_wnd, IDC_TOOLTIPS, BST_CHECKED);
        CheckDlgButton(m_wnd, IDC_SKIN_CURSORS, BST_CHECKED);
        CheckDlgButton(m_wnd, IDC_TITLE_EGG, BST_UNCHECKED);
        CheckDlgButton(m_wnd, IDC_PLVIEW_FONT, BST_CHECKED);
        CheckDlgButton(m_wnd, IDC_TITLE_BITMAP, BST_CHECKED);
        CheckDlgButton(m_wnd, IDC_TITLE_ALT, BST_UNCHECKED);
        uSetDlgItemText(m_wnd, IDC_FORMAT_MAIN, config::default_format_main);
        uSetDlgItemText(m_wnd, IDC_FORMAT_PL, config::default_format_playlist);
        uSetDlgItemText(m_wnd, IDC_FORMAT_PL_SHADE, config::default_format_playlist_shade);
        update_enabled();
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
            if (LOWORD(wp) == IDC_TITLE_BITMAP || LOWORD(wp) == IDC_BALANCE_DSP) self->update_enabled();
            if (HIWORD(wp) == BN_CLICKED || (HIWORD(wp) == EN_CHANGE && self->m_initialized))
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
        CheckDlgButton(m_wnd, IDC_SNAP, config::snap.get() ? BST_CHECKED : BST_UNCHECKED);
        SetDlgItemInt(m_wnd, IDC_SNAP_PX, static_cast<UINT>(config::snap_distance.get()), FALSE);
        SendDlgItemMessageW(m_wnd, IDC_SNAP_PX, EM_SETLIMITTEXT, 2, 0);
        CheckDlgButton(m_wnd, IDC_SMOOTH_RESIZE, config::smooth_resize.get() ? BST_CHECKED : BST_UNCHECKED);
        CheckRadioButton(m_wnd, IDC_DSIZE_ALL, IDC_DSIZE_EACH, config::double_size_all.get() ? IDC_DSIZE_ALL : IDC_DSIZE_EACH);
        CheckDlgButton(m_wnd, IDC_CLUTTERBAR, config::clutterbar.get() ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(m_wnd, IDC_OLDSCHOOL_PL, config::oldschool_pl_buttons.get() ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(m_wnd, IDC_TOOLTIPS, config::tooltips.get() ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(m_wnd, IDC_SKIN_CURSORS, config::skin_cursors.get() ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(m_wnd, IDC_TITLE_EGG, config::title_easter_egg.get() ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(m_wnd, IDC_PLVIEW_FONT, config::playlist_view_skin_font.get() ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(m_wnd, IDC_TITLE_BITMAP, config::title_bitmap_font.get() ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(m_wnd, IDC_TITLE_ALT, config::title_font_alt.get() ? BST_CHECKED : BST_UNCHECKED);
        uSetDlgItemText(m_wnd, IDC_FORMAT_MAIN, config::format_main.get());
        uSetDlgItemText(m_wnd, IDC_FORMAT_PL, config::format_playlist.get());
        uSetDlgItemText(m_wnd, IDC_FORMAT_PL_SHADE, config::format_playlist_shade.get());
        update_enabled();
        m_initialized = true;
    }

    // The alternative style only applies to the Windows-font title, like Winamp.
    void update_enabled() {
        EnableWindow(GetDlgItem(m_wnd, IDC_TITLE_ALT), !checked(IDC_TITLE_BITMAP));
        // The snap only matters while the balance slider works, i.e. with the balance DSP.
        const bool dsp = checked(IDC_BALANCE_DSP);
        for (int id : { IDC_BALANCE_SNAP_LABEL, IDC_BALANCE_SNAP, IDC_BALANCE_SNAP_PCT }) EnableWindow(GetDlgItem(m_wnd, id), dsp);
    }

    bool checked(int id) const { return IsDlgButtonChecked(m_wnd, id) == BST_CHECKED; }
    pfc::string8 text(int id) const { return uGetDlgItemText(m_wnd, id); }

    int snap_value() const {
        BOOL ok = FALSE;
        const UINT v = GetDlgItemInt(m_wnd, IDC_BALANCE_SNAP, &ok, FALSE);
        return ok ? static_cast<int>(std::min(v, 99u)) : default_snap;
    }

    int dock_value() const {
        BOOL ok = FALSE;
        const UINT v = GetDlgItemInt(m_wnd, IDC_SNAP_PX, &ok, FALSE);
        return ok ? static_cast<int>(std::clamp(v, 1u, 99u)) : default_dock;
    }

    bool has_changed() const {
        if (!m_wnd) return false;
        return checked(IDC_WINAMP_HOTKEYS) != config::winamp_hotkeys.get()
            || checked(IDC_BALANCE_DSP) != balance::enabled()
            || snap_value() != static_cast<int>(config::balance_snap.get())
            || checked(IDC_DIM_TITLEBARS) != config::dim_titlebars.get()
            || checked(IDC_SNAP) != config::snap.get()
            || dock_value() != static_cast<int>(config::snap_distance.get())
            || checked(IDC_SMOOTH_RESIZE) != config::smooth_resize.get()
            || checked(IDC_DSIZE_ALL) != config::double_size_all.get()
            || checked(IDC_CLUTTERBAR) != config::clutterbar.get()
            || checked(IDC_OLDSCHOOL_PL) != config::oldschool_pl_buttons.get()
            || checked(IDC_TOOLTIPS) != config::tooltips.get()
            || checked(IDC_SKIN_CURSORS) != config::skin_cursors.get()
            || checked(IDC_TITLE_EGG) != config::title_easter_egg.get()
            || checked(IDC_PLVIEW_FONT) != config::playlist_view_skin_font.get()
            || checked(IDC_TITLE_BITMAP) != config::title_bitmap_font.get()
            || checked(IDC_TITLE_ALT) != config::title_font_alt.get()
            || text(IDC_FORMAT_MAIN) != config::format_main.get()
            || text(IDC_FORMAT_PL) != config::format_playlist.get()
            || text(IDC_FORMAT_PL_SHADE) != config::format_playlist_shade.get();
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
