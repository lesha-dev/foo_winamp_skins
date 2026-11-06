#include "stdafx.h"
#include "config.h"
#include "guids.h"
#include "main_renderer.h"
#include "main_window.h"
#include "player.h"
#include "resource.h"
#include "skins_folder.h"
#include "version.h"

#include <foobar2000/SDK/coreDarkMode.h>

#include <commctrl.h>
#include <shellapi.h>

namespace wsz {
namespace {

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

int CALLBACK font_found(const LOGFONTW*, const TEXTMETRICW*, DWORD, LPARAM lp) {
    *reinterpret_cast<bool*>(lp) = true;
    return 0;
}

bool font_installed(const wchar_t* face) {
    LOGFONTW lf = {};
    lf.lfCharSet = DEFAULT_CHARSET;
    wcsncpy_s(lf.lfFaceName, face, _TRUNCATE);
    bool found = false;
    HDC dc = GetDC(nullptr);
    EnumFontFamiliesExW(dc, &lf, &font_found, reinterpret_cast<LPARAM>(&found), 0);
    ReleaseDC(nullptr, dc);
    return found;
}

// Windows' own icon font: Segoe Fluent Icons (Windows 11), Segoe MDL2 Assets (Windows 10).
const wchar_t* icon_font_face() {
    for (const wchar_t* face : { L"Segoe Fluent Icons", L"Segoe MDL2 Assets" })
        if (font_installed(face)) return face;
    return nullptr;
}

struct FolderButton {
    int id;
    const wchar_t* glyph;  // in the icon font
    const wchar_t* symbol; // in Segoe UI Symbol (Windows 7); null = shell folder icon
    const wchar_t* text;   // last resort
    const wchar_t* tip;
};

const FolderButton folder_buttons[] = {
    { IDC_BROWSE_FOLDER, L"\xE712", L"\x2026", L"...", L"Choose skins folder" },
    { IDC_OPEN_FOLDER, L"\xE8B7", nullptr, L"Open", L"Open skins folder in Explorer" },
    { IDC_REFRESH, L"\xE72C", L"\x21BB", L"R", L"Refresh skin list" },
};

// Skin selection is edited here and only take effect on Apply.
class PrefsInstance : public preferences_page_instance {
public:
    explicit PrefsInstance(preferences_page_callback::ptr callback) : m_callback(callback) {}

    // Separate from the constructor: the dialog proc takes references, which must not happen before
    // the object is owned by a service_ptr.
    void create(HWND parent) {
        CreateDialogParamW(core_api::get_my_instance(), MAKEINTRESOURCEW(IDD_PREFS), parent, &s_dlgproc, reinterpret_cast<LPARAM>(this));
    }

    t_uint32 get_state() override {
        t_uint32 s = preferences_state::dark_mode_supported;
        if (has_changed()) s |= preferences_state::changed;
        return s;
    }
    fb2k::hwnd_t get_wnd() override { return m_wnd; }

    void apply() override {
        if (folder_changed()) skins_folder::set_path(m_folder);
        const std::wstring current = wide(config::skin_path.get());
        if (_wcsicmp(m_pending.c_str(), current.c_str()) != 0) {
            MainWindow* w = MainWindow::instance();
            if (m_pending.empty()) {
                if (w) w->unload_skin();
                else config::skin_path = "";
            } else {
                if (w) w->load_skin(utf8(m_pending), true);
                else config::skin_path = utf8(m_pending);
            }
        }
        changed();
    }

    void reset() override {}

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
            if (self->m_icon_font) { DeleteObject(self->m_icon_font); self->m_icon_font = nullptr; }
            if (self->m_folder_icon) { DestroyIcon(self->m_folder_icon); self->m_folder_icon = nullptr; }
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
        m_folder = skins_folder::path();
        uSetDlgItemText(m_wnd, IDC_FOLDER, utf8(m_folder));
        init_folder_buttons();
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
        case IDC_BROWSE_FOLDER:
            if (code == BN_CLICKED) {
                std::wstring dir;
                if (skins_folder::browse(m_wnd, m_folder, dir)) {
                    m_folder = dir;
                    uSetDlgItemText(m_wnd, IDC_FOLDER, utf8(m_folder));
                    refresh_list();
                    changed();
                }
            }
            break;
        case IDC_OPEN_FOLDER:
            if (code == BN_CLICKED) skins_folder::open_in_explorer(m_folder);
            break;
        case IDC_REFRESH:
            if (code == BN_CLICKED) refresh_list();
            break;
        }
    }

    void init_folder_buttons() {
        const wchar_t* face = icon_font_face();
        const bool fluent = face != nullptr;
        if (!face && font_installed(L"Segoe UI Symbol")) face = L"Segoe UI Symbol";
        if (face) {
            RECT rc;
            GetClientRect(GetDlgItem(m_wnd, IDC_REFRESH), &rc);
            LOGFONTW lf = {};
            lf.lfHeight = -std::max(8L, (rc.bottom - rc.top) * (fluent ? 9 : 11) / 16);
            lf.lfCharSet = DEFAULT_CHARSET;
            lf.lfQuality = CLEARTYPE_QUALITY;
            wcsncpy_s(lf.lfFaceName, face, _TRUNCATE);
            m_icon_font = CreateFontIndirectW(&lf);
        }
        HWND tip = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr, WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX,
            CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, m_wnd, nullptr, core_api::get_my_instance(), nullptr);
        if (tip) m_dark.AddCtrlAuto(tip);
        for (const auto& b : folder_buttons) {
            HWND btn = GetDlgItem(m_wnd, b.id);
            const wchar_t* text = m_icon_font ? (fluent ? b.glyph : b.symbol) : b.text;
            if (!text) {
                // Windows 7: the shell's own small folder icon.
                SHSTOCKICONINFO sii = {};
                sii.cbSize = sizeof(sii);
                if (SUCCEEDED(SHGetStockIconInfo(SIID_FOLDER, SHGSI_ICON | SHGSI_SMALLICON, &sii))) {
                    m_folder_icon = sii.hIcon;
                    SetWindowLongPtrW(btn, GWL_STYLE, GetWindowLongPtrW(btn, GWL_STYLE) | BS_ICON);
                    SendMessageW(btn, BM_SETIMAGE, IMAGE_ICON, reinterpret_cast<LPARAM>(m_folder_icon));
                    SetWindowTextW(btn, L"");
                } else {
                    SetWindowTextW(btn, b.text);
                }
            } else {
                if (m_icon_font) SendMessageW(btn, WM_SETFONT, reinterpret_cast<WPARAM>(m_icon_font), FALSE);
                SetWindowTextW(btn, text);
            }
            if (!tip) continue;
            TTTOOLINFOW ti = {};
            ti.cbSize = sizeof(ti);
            ti.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
            ti.hwnd = m_wnd;
            ti.uId = reinterpret_cast<UINT_PTR>(btn);
            ti.lpszText = const_cast<wchar_t*>(b.tip);
            SendMessageW(tip, TTM_ADDTOOLW, 0, reinterpret_cast<LPARAM>(&ti));
        }
    }

    void refresh_list() {
        m_skins = skins_folder::list(m_folder);
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
        // The selected skin is gone (deleted, or another folder): select none.
        if (sel < 0 && !m_pending.empty()) select_pending({});
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
        // Page background (dark mode aware: the dialog answers WM_CTLCOLORDLG), seen around and through the skin's shape.
        HBRUSH bg = reinterpret_cast<HBRUSH>(SendMessageW(m_wnd, WM_CTLCOLORDLG, reinterpret_cast<WPARAM>(dc), reinterpret_cast<LPARAM>(m_wnd)));
        FillRect(dc, &rc, bg ? bg : GetSysColorBrush(COLOR_BTNFACE));
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
        // The skin's region.txt shapes the real main window: mask the preview the same way.
        HRGN shape = nullptr;
        auto& regions = m_preview->regions();
        if (auto it = regions.find("normal"); it != regions.end()) {
            std::vector<POINT> points;
            std::vector<INT> counts;
            for (auto& poly : it->second) {
                for (auto pt : poly.points) points.push_back({ dst.left + pt.x * w / bw, dst.top + pt.y * h / bh });
                counts.push_back(static_cast<INT>(poly.points.size()));
            }
            shape = CreatePolyPolygonRgn(points.data(), counts.data(), static_cast<int>(counts.size()), WINDING);
        }
        const int saved = SaveDC(dc);
        if (shape) SelectClipRgn(dc, shape); // inside the preview control anyway
        m_canvas.present(dc, dst);
        RestoreDC(dc, saved);
        if (shape) DeleteObject(shape);
    }

    bool has_changed() const {
        if (!m_wnd) return false;
        return folder_changed() || _wcsicmp(m_pending.c_str(), wide(config::skin_path.get()).c_str()) != 0;
    }

    bool folder_changed() const { return _wcsicmp(m_folder.c_str(), skins_folder::path().c_str()) != 0; }

    void changed() { m_callback->on_state_changed(); }

    HWND m_wnd = nullptr;
    preferences_page_callback::ptr m_callback;
    fb2k::CCoreDarkModeHooks m_dark;
    HFONT m_icon_font = nullptr;
    HICON m_folder_icon = nullptr;

    std::vector<skins_folder::Entry> m_skins;
    std::wstring m_pending; // skin path applied on Apply; empty = no skin
    std::wstring m_folder;  // skins folder applied on Apply
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
