#include "stdafx.h"
#include "balance_dsp.h"
#include "config.h"
#include "guids.h"
#include "main_renderer.h"
#include "main_window.h"
#include "player.h"
#include "resource.h"
#include "skins_folder.h"
#include "version.h"

#include <foobar2000/SDK/coreDarkMode.h>

namespace wsz {
namespace {

constexpr int default_snap = 25;

std::wstring wide(const char* s) { return pfc::stringcvt::string_wide_from_utf8(s).get_ptr(); }
pfc::string8 utf8(const std::wstring& s) { return pfc::stringcvt::string_utf8_from_wide(s.c_str()).get_ptr(); }

// Edit controls need CRLF line breaks.
pfc::string8 to_crlf(const char* s) {
    pfc::string8 out;
    for (; *s; ++s) {
        if (*s == '\r') { if (s[1] == '\n') ++s; out << "\r\n"; }
        else if (*s == '\n') out << "\r\n";
        else out.add_char(*s);
    }
    return out;
}

std::wstring file_title(const std::wstring& path) {
    const size_t slash = path.find_last_of(L"\\/");
    std::wstring name = slash == std::wstring::npos ? path : path.substr(slash + 1);
    const size_t dot = name.find_last_of(L'.');
    if (dot != std::wstring::npos && dot > 0) name.resize(dot);
    return name;
}

// Skin selection and options are edited here and only take effect on Apply.
class PrefsInstance : public preferences_page_instance {
public:
    explicit PrefsInstance(preferences_page_callback::ptr callback) : m_callback(callback) {}

    // Separate from the constructor: the dialog proc takes references, which must not happen before
    // the object is owned by a service_ptr.
    void create(HWND parent) {
        CreateDialogParamW(core_api::get_my_instance(), MAKEINTRESOURCEW(IDD_PREFS), parent, &s_dlgproc, reinterpret_cast<LPARAM>(this));
    }

    t_uint32 get_state() override {
        t_uint32 s = preferences_state::resettable | preferences_state::dark_mode_supported;
        if (has_changed()) s |= preferences_state::changed;
        return s;
    }
    fb2k::hwnd_t get_wnd() override { return m_wnd; }

    void apply() override {
        const std::wstring current = wide(config::skin_path.get());
        if (_wcsicmp(m_pending.c_str(), current.c_str()) != 0) {
            MainWindow* w = MainWindow::instance();
            if (m_pending.empty()) {
                if (w) w->unload_skin();
                else config::skin_path = "";
            } else if (w) {
                w->load_skin(utf8(m_pending), true);
            } else {
                config::skin_path = utf8(m_pending);
            }
        }
        config::winamp_hotkeys = checked(IDC_WINAMP_HOTKEYS);
        balance::set_enabled(checked(IDC_BALANCE_DSP));
        config::balance_snap = snap_value();
        if (MainWindow* w = MainWindow::instance()) w->invalidate();
        changed();
    }

    void reset() override {
        CheckDlgButton(m_wnd, IDC_WINAMP_HOTKEYS, BST_CHECKED);
        CheckDlgButton(m_wnd, IDC_BALANCE_DSP, BST_CHECKED);
        SetDlgItemInt(m_wnd, IDC_BALANCE_SNAP, default_snap, FALSE);
        changed();
    }

private:
    static INT_PTR CALLBACK s_dlgproc(HWND wnd, UINT msg, WPARAM wp, LPARAM lp) {
        if (msg == WM_INITDIALOG) {
            auto* self = reinterpret_cast<PrefsInstance*>(lp);
            self->m_wnd = wnd;
            SetWindowLongPtrW(wnd, DWLP_USER, lp);
        }
        auto* self = reinterpret_cast<PrefsInstance*>(GetWindowLongPtrW(wnd, DWLP_USER));
        if (!self) return FALSE;
        // Modal dialogs spawned from handlers can destroy the page under us; stay alive until we return.
        service_ptr_t<PrefsInstance> keep(self);
        const INT_PTR ret = self->dlgproc(msg, wp, lp);
        if (msg == WM_DESTROY) {
            SetWindowLongPtrW(wnd, DWLP_USER, 0);
            self->m_wnd = nullptr;
        }
        return ret;
    }

    INT_PTR dlgproc(UINT msg, WPARAM wp, LPARAM lp) {
        switch (msg) {
        case WM_INITDIALOG:
            on_init();
            return TRUE;
        case WM_COMMAND:
            on_command(LOWORD(wp), HIWORD(wp));
            return TRUE;
        case WM_DRAWITEM: {
            auto* dis = reinterpret_cast<DRAWITEMSTRUCT*>(lp);
            if (dis->CtlID != IDC_PREVIEW) return FALSE;
            draw_preview(dis->hDC, dis->rcItem);
            return TRUE;
        }
        }
        return FALSE;
    }

    void on_init() {
        m_dark.AddDialogWithControls(m_wnd);
        m_pending = wide(config::skin_path.get());
        uSetDlgItemText(m_wnd, IDC_FOLDER, utf8(skins_folder::path()));
        CheckDlgButton(m_wnd, IDC_WINAMP_HOTKEYS, config::winamp_hotkeys.get() ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(m_wnd, IDC_BALANCE_DSP, balance::enabled() ? BST_CHECKED : BST_UNCHECKED);
        SetDlgItemInt(m_wnd, IDC_BALANCE_SNAP, static_cast<UINT>(config::balance_snap.get()), FALSE);
        SendDlgItemMessageW(m_wnd, IDC_BALANCE_SNAP, EM_SETLIMITTEXT, 2, 0);
        m_initialized = true;
        refresh_list();
        load_preview();
    }

    void on_command(UINT id, UINT code) {
        switch (id) {
        case IDC_SKIN_LIST:
            if (code == LBN_SELCHANGE) {
                const int sel = static_cast<int>(SendDlgItemMessageW(m_wnd, IDC_SKIN_LIST, LB_GETCURSEL, 0, 0));
                const size_t idx = static_cast<size_t>(sel);
                if (sel >= 0 && idx < m_skins.size()) select_pending(m_skins[idx].path);
            }
            break;
        case IDC_OPEN_FOLDER:
            if (code == BN_CLICKED) skins_folder::open_in_explorer();
            break;
        case IDC_REFRESH:
            if (code == BN_CLICKED) refresh_list();
            break;
        case IDC_LOAD_FILE:
            if (code == BN_CLICKED) browse();
            break;
        case IDC_UNLOAD:
            if (code == BN_CLICKED) {
                SendDlgItemMessageW(m_wnd, IDC_SKIN_LIST, LB_SETCURSEL, static_cast<WPARAM>(-1), 0);
                select_pending(L"");
            }
            break;
        case IDC_WINAMP_HOTKEYS:
        case IDC_BALANCE_DSP:
            if (code == BN_CLICKED) changed();
            break;
        case IDC_BALANCE_SNAP:
            if (code == EN_CHANGE && m_initialized) changed();
            break;
        }
    }

    void refresh_list() {
        m_skins = skins_folder::list();
        HWND list = GetDlgItem(m_wnd, IDC_SKIN_LIST);
        SendMessageW(list, WM_SETREDRAW, FALSE, 0);
        SendMessageW(list, LB_RESETCONTENT, 0, 0);
        int sel = -1;
        for (size_t i = 0; i < m_skins.size(); ++i) {
            SendMessageW(list, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(m_skins[i].name.c_str()));
            if (!m_pending.empty() && _wcsicmp(m_skins[i].path.c_str(), m_pending.c_str()) == 0) sel = static_cast<int>(i);
        }
        SendMessageW(list, LB_SETCURSEL, static_cast<WPARAM>(sel), 0);
        SendMessageW(list, WM_SETREDRAW, TRUE, 0);
        InvalidateRect(list, nullptr, TRUE);
    }

    void browse() {
        pfc::string8 path = utf8(m_pending);
        const pfc::string8 dir = utf8(skins_folder::path());
        if (!uGetOpenFileName(m_wnd, "Winamp skins (*.wsz;*.zip)|*.wsz;*.zip|All files|*.*", 0, "wsz",
                "Load Winamp skin", path.is_empty() ? dir.get_ptr() : nullptr, path, FALSE))
            return;
        if (!m_wnd) return; // page went away while the dialog was open
        select_pending(wide(path));
        refresh_list(); // selects it if the file is in the skins folder
    }

    void select_pending(const std::wstring& path) {
        m_pending = path;
        load_preview();
        changed();
    }

    void load_preview() {
        m_preview.reset();
        pfc::string8 info;
        if (m_pending.empty()) {
            info = "No skin selected.";
        } else {
            try {
                m_preview = Skin::load(utf8(m_pending));
                info = m_preview->readme().c_str();
                if (!m_preview->warnings().empty()) info << (info.is_empty() ? "[" : "\n\n[") << m_preview->warnings().size() << " warning(s), see console after applying]";
            } catch (const std::exception& e) {
                info = pfc::format("Cannot load skin:\n", e.what());
            }
        }
        uSetDlgItemText(m_wnd, IDC_SKIN_INFO, to_crlf(info));
        if (m_preview) {
            MainState st;
            st.active = true;
            st.volume = player::db_to_slider(playback_control::get()->get_volume());
            st.marquee = utf8(file_title(m_pending)).get_ptr();
            m_canvas.resize(MainWindow::base_width, MainWindow::base_height);
            render_main(m_canvas, *m_preview, st);
        }
        InvalidateRect(GetDlgItem(m_wnd, IDC_PREVIEW), nullptr, FALSE);
    }

    void draw_preview(HDC dc, const RECT& rc) {
        FillRect(dc, &rc, static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
        if (!m_preview) return;
        const int aw = rc.right - rc.left, ah = rc.bottom - rc.top;
        const int bw = MainWindow::base_width, bh = MainWindow::base_height;
        // Largest integer scale that fits; shrink smoothly if even 1x doesn't.
        int w, h;
        const int scale = std::min(aw / bw, ah / bh);
        if (scale >= 1) { w = bw * scale; h = bh * scale; }
        else if (aw * bh < ah * bw) { w = aw; h = aw * bh / bw; }
        else { h = ah; w = ah * bw / bh; }
        const RECT dst = { rc.left + (aw - w) / 2, rc.top + (ah - h) / 2, rc.left + (aw - w) / 2 + w, rc.top + (ah - h) / 2 + h };
        m_canvas.present(dc, dst);
    }

    bool checked(int id) const { return IsDlgButtonChecked(m_wnd, id) == BST_CHECKED; }

    int snap_value() const {
        BOOL ok = FALSE;
        const UINT v = GetDlgItemInt(m_wnd, IDC_BALANCE_SNAP, &ok, FALSE);
        return ok ? static_cast<int>(std::min(v, 99u)) : default_snap;
    }

    bool has_changed() const {
        if (!m_wnd) return false;
        if (_wcsicmp(m_pending.c_str(), wide(config::skin_path.get()).c_str()) != 0) return true;
        if (checked(IDC_WINAMP_HOTKEYS) != config::winamp_hotkeys.get()) return true;
        if (checked(IDC_BALANCE_DSP) != balance::enabled()) return true;
        return snap_value() != static_cast<int>(config::balance_snap.get());
    }

    void changed() { m_callback->on_state_changed(); }

    HWND m_wnd = nullptr;
    preferences_page_callback::ptr m_callback;
    fb2k::CCoreDarkModeHooks m_dark;
    bool m_initialized = false;

    std::vector<skins_folder::Entry> m_skins;
    std::wstring m_pending; // skin path applied on Apply; empty = no skin
    std::unique_ptr<Skin> m_preview;
    Canvas m_canvas;
};

class PrefsPage : public preferences_page_v3 {
public:
    const char* get_name() override { return COMPONENT_NAME; }
    GUID get_guid() override { return guids::prefs_page; }
    GUID get_parent_guid() override { return preferences_page::guid_display; }
    preferences_page_instance::ptr instantiate(fb2k::hwnd_t parent, preferences_page_callback::ptr callback) override {
        auto obj = fb2k::service_new<PrefsInstance>(callback);
        obj->create(parent);
        if (!obj->get_wnd()) throw exception_win32(GetLastError());
        return obj;
    }
};

preferences_page_factory_t<PrefsPage> g_prefs_page;

} // namespace
} // namespace wsz
