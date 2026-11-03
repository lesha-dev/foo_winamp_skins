#include "stdafx.h"
#include "config.h"
#include "guids.h"
#include "main_window.h"
#include "resource.h"
#include "visualizer.h"

#include <commctrl.h>
#include <foobar2000/SDK/coreDarkMode.h>

// Display > Winamp Skins > Visualization: Winamp's classic vis options.
namespace wsz {
namespace {

const wchar_t* const refresh_names[] = { L"Full", L"Half", L"Quarter", L"Eighth" };
const wchar_t* const speed_names[] = { L"Slowest", L"Slow", L"Moderate", L"Fast", L"Fastest" };

struct VisSettings {
    VisMode mode = VisMode::spectrum;
    VisOptions opt;
    bool operator==(const VisSettings& o) const {
        return mode == o.mode && opt.analyzer == o.opt.analyzer && opt.thin_bands == o.opt.thin_bands && opt.peaks == o.opt.peaks
            && opt.scope == o.opt.scope && opt.refresh == o.opt.refresh && opt.falloff == o.opt.falloff
            && opt.peak_falloff == o.opt.peak_falloff;
    }
};

VisSettings stored() {
    VisSettings s;
    s.mode = static_cast<VisMode>(std::clamp<int>(static_cast<int>(config::vis_mode.get()), 0, 2));
    s.opt = config::vis_options();
    return s;
}

class VisPrefsInstance : public preferences_page_instance {
public:
    explicit VisPrefsInstance(preferences_page_callback::ptr callback) : m_callback(callback) {}

    void create(HWND parent) {
        CreateDialogParamW(core_api::get_my_instance(), MAKEINTRESOURCEW(IDD_PREFS_VIS), parent, &s_dlgproc, reinterpret_cast<LPARAM>(this));
    }

    t_uint32 get_state() override {
        t_uint32 s = preferences_state::resettable | preferences_state::dark_mode_supported;
        if (m_wnd && !(read() == stored())) s |= preferences_state::changed;
        return s;
    }
    fb2k::hwnd_t get_wnd() override { return m_wnd; }

    void apply() override {
        const VisSettings s = read();
        if (MainWindow* w = MainWindow::instance()) {
            w->set_vis_options(s.opt);
            w->set_vis_mode(s.mode);
        } else {
            config::set_vis_options(s.opt);
            config::vis_mode = static_cast<int>(s.mode);
        }
        changed();
    }

    void reset() override {
        show(VisSettings{});
        changed();
    }

private:
    static INT_PTR CALLBACK s_dlgproc(HWND wnd, UINT msg, WPARAM wp, LPARAM lp) {
        if (msg == WM_INITDIALOG) {
            reinterpret_cast<VisPrefsInstance*>(lp)->m_wnd = wnd;
            SetWindowLongPtrW(wnd, DWLP_USER, lp);
        }
        auto* self = reinterpret_cast<VisPrefsInstance*>(GetWindowLongPtrW(wnd, DWLP_USER));
        if (!self) return FALSE;
        service_ptr_t<VisPrefsInstance> keep(self);
        switch (msg) {
        case WM_INITDIALOG:
            self->on_init();
            return TRUE;
        case WM_COMMAND:
            if (HIWORD(wp) == BN_CLICKED) self->changed();
            return TRUE;
        case WM_HSCROLL:
            self->update_labels();
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
        SendDlgItemMessageW(m_wnd, IDC_VIS_REFRESH, TBM_SETRANGE, FALSE, MAKELPARAM(0, 3));
        SendDlgItemMessageW(m_wnd, IDC_VIS_FALLOFF, TBM_SETRANGE, FALSE, MAKELPARAM(0, 4));
        SendDlgItemMessageW(m_wnd, IDC_VIS_PFALLOFF, TBM_SETRANGE, FALSE, MAKELPARAM(0, 4));
        show(stored());
    }

    void show(const VisSettings& s) {
        CheckRadioButton(m_wnd, IDC_VIS_SPECTRUM, IDC_VIS_OFF, IDC_VIS_SPECTRUM + static_cast<int>(s.mode));
        CheckRadioButton(m_wnd, IDC_VIS_NORMAL, IDC_VIS_LINE, IDC_VIS_NORMAL + s.opt.analyzer);
        CheckRadioButton(m_wnd, IDC_VIS_THICK, IDC_VIS_THIN, s.opt.thin_bands ? IDC_VIS_THIN : IDC_VIS_THICK);
        CheckDlgButton(m_wnd, IDC_VIS_PEAKS, s.opt.peaks ? BST_CHECKED : BST_UNCHECKED);
        CheckRadioButton(m_wnd, IDC_VIS_DOTS, IDC_VIS_SOLID, IDC_VIS_DOTS + s.opt.scope);
        SendDlgItemMessageW(m_wnd, IDC_VIS_REFRESH, TBM_SETPOS, TRUE, s.opt.refresh - 1);
        SendDlgItemMessageW(m_wnd, IDC_VIS_FALLOFF, TBM_SETPOS, TRUE, s.opt.falloff);
        SendDlgItemMessageW(m_wnd, IDC_VIS_PFALLOFF, TBM_SETPOS, TRUE, s.opt.peak_falloff);
        update_labels();
    }

    int pos(int id) const { return static_cast<int>(SendDlgItemMessageW(m_wnd, id, TBM_GETPOS, 0, 0)); }
    bool checked(int id) const { return IsDlgButtonChecked(m_wnd, id) == BST_CHECKED; }
    int radio(int first, int count) const {
        for (int i = 0; i < count; ++i)
            if (checked(first + i)) return i;
        return 0;
    }

    VisSettings read() const {
        VisSettings s;
        s.mode = static_cast<VisMode>(radio(IDC_VIS_SPECTRUM, 3));
        s.opt.analyzer = radio(IDC_VIS_NORMAL, 3);
        s.opt.thin_bands = checked(IDC_VIS_THIN);
        s.opt.peaks = checked(IDC_VIS_PEAKS);
        s.opt.scope = radio(IDC_VIS_DOTS, 3);
        s.opt.refresh = std::clamp(pos(IDC_VIS_REFRESH), 0, 3) + 1;
        s.opt.falloff = std::clamp(pos(IDC_VIS_FALLOFF), 0, 4);
        s.opt.peak_falloff = std::clamp(pos(IDC_VIS_PFALLOFF), 0, 4);
        return s;
    }

    void update_labels() {
        SetDlgItemTextW(m_wnd, IDC_VIS_REFRESH_TXT, refresh_names[std::clamp(pos(IDC_VIS_REFRESH), 0, 3)]);
        SetDlgItemTextW(m_wnd, IDC_VIS_FALLOFF_TXT, speed_names[std::clamp(pos(IDC_VIS_FALLOFF), 0, 4)]);
        SetDlgItemTextW(m_wnd, IDC_VIS_PFALLOFF_TXT, speed_names[std::clamp(pos(IDC_VIS_PFALLOFF), 0, 4)]);
    }

    void changed() { m_callback->on_state_changed(); }

    HWND m_wnd = nullptr;
    preferences_page_callback::ptr m_callback;
    fb2k::CCoreDarkModeHooks m_dark;
};

class VisPrefsPage : public preferences_page_v3 {
public:
    const char* get_name() override { return "Visualization"; }
    GUID get_guid() override { return guids::prefs_vis_page; }
    GUID get_parent_guid() override { return guids::prefs_page; }
    preferences_page_instance::ptr instantiate(fb2k::hwnd_t parent, preferences_page_callback::ptr callback) override {
        auto obj = fb2k::service_new<VisPrefsInstance>(callback);
        obj->create(parent);
        if (!obj->get_wnd()) throw exception_win32(GetLastError());
        return obj;
    }
};

preferences_page_factory_t<VisPrefsPage> g_vis_prefs_page;

} // namespace
} // namespace wsz
