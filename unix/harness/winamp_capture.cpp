// Development tool (not shipped): runs a portable Winamp once per skin and saves every visible
// top-level window of its process as a PNG, for side-by-side comparison with modern_harness.
//
//   winamp_capture.exe <winamp_dir> <out_dir> [--wait secs] [--list file] [--modes plan.txt] [--tracks folder] [--close-running] [skin.wal ...]
//
// <winamp_dir> must be a portable copy (paths.ini with inidir=<winamp_dir>\Settings). On first use
// the current Settings folder is saved as Settings.clean; before each skin Settings is restored
// from it, so every skin starts with its defaults; the Settings found at the start are put back at the
// end. A running Winamp is waited for (single instance), or closed with --close-running. Output: <out_dir>\<skin>\NN_<title>_WxH.png and
// windows.txt (first line "#workarea<TAB>l,t,r,b": the primary work area, which skin scripts read as the
// viewport; then one line per window: index, class, title, rect, ex-style, state, "t=" the local time it was taken
// (HH:MM:SS.mmm; also in tracks.txt and modes.txt), child texts). A skin the --modes plan
// marks "#about" (it declares skin.about.group) also gets about_WxH.png: that group as Preferences > Skins >
// Modern Skins > Current skin shows it. Preferences isn't opened for other skins (or without a plan).

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commctrl.h>
#include <tlhelp32.h>
#include <wincodec.h>
#include <wrl/client.h>

#include "capture_util.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace fs = std::filesystem;

// Sets skin= in [Winamp] and [WinampReg] of Winamp.ini, keeping the file's encoding as-is
// (Winamp writes it as ANSI/UTF-8 without BOM).
static bool set_skin(const fs::path& ini, const std::wstring& skin) {
    std::ifstream in(ini, std::ios::binary);
    if (!in) return false;
    std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    in.close();
    const std::string value = "skin=" + utf8(skin);
    std::string out, section;
    bool done_winamp = false;
    size_t pos = 0;
    while (pos < text.size()) {
        size_t e = text.find('\n', pos);
        std::string line = text.substr(pos, e == std::string::npos ? std::string::npos : e - pos + 1);
        pos = e == std::string::npos ? text.size() : e + 1;
        std::string bare = line;
        while (!bare.empty() && (bare.back() == '\n' || bare.back() == '\r')) bare.pop_back();
        if (!bare.empty() && bare[0] == '[') {
            if (section == "[Winamp]" && !done_winamp) { out += value + "\r\n"; done_winamp = true; }
            section = bare;
        } else if ((section == "[Winamp]" || section == "[WinampReg]") && _strnicmp(bare.c_str(), "skin=", 5) == 0) {
            out += value + line.substr(bare.size());
            if (section == "[Winamp]") done_winamp = true;
            continue;
        }
        out += line;
    }
    if (!done_winamp) out += (section == "[Winamp]" ? "" : "[Winamp]\r\n") + value + "\r\n";
    std::ofstream o(ini, std::ios::binary | std::ios::trunc);
    o << out;
    return static_cast<bool>(o);
}

// [section] key=value in an ini file (added if missing).
static void set_ini_value(const fs::path& ini, const std::string& section, const std::string& key, const std::string& value) {
    std::ifstream in(ini, std::ios::binary);
    std::string t((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    in.close();
    const size_t sec = t.find("[" + section + "]");
    if (sec == std::string::npos) return;
    const size_t next = t.find("\n[", sec + 1), end = next == std::string::npos ? t.size() : next + 1;
    const size_t k = t.find("\n" + key + "=", sec);
    if (k != std::string::npos && k < end) {
        const size_t v = k + 2 + key.size(), e = t.find_first_of("\r\n", v);
        t.replace(v, (e == std::string::npos ? t.size() : e) - v, value);
    } else {
        t.insert(t.find('\n', sec) + 1, key + "=" + value + "\r\n");
    }
    std::ofstream o(ini, std::ios::binary | std::ios::trunc);
    o << t;
}

static void set_winamp_value(const fs::path& ini, const std::string& key, const std::string& value) { set_ini_value(ini, "Winamp", key, value); }

struct Found { HWND hwnd; RECT rect; };

// Any winamp.exe running (the user's, or ours still shutting down). Winamp is single-instance: a second
// one hands its start-up to the running one and quits, which would leave nothing to capture.
// WINAMP_CAPTURE_TIMES=1: each phase's time since the tool started, on stdout.
static void mark(const char* what) {
    static const ULONGLONG t0 = GetTickCount64();
    static const bool on = getenv("WINAMP_CAPTURE_TIMES") != nullptr;
    if (on) printf("  %6llu ms  %s\n", GetTickCount64() - t0, what);
}

static bool g_close_running = false; // --close-running: ask a running Winamp to close instead of waiting

// which: the running one's PID and path, for the message; pid: its PID.
static bool winamp_running(std::wstring* which = nullptr, DWORD* pid = nullptr) {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return FindWindowW(L"Winamp v1.x", nullptr) != nullptr;
    PROCESSENTRY32W pe{};
    pe.dwSize = sizeof(pe);
    bool found = false;
    for (BOOL ok = Process32FirstW(snap, &pe); ok && !found; ok = Process32NextW(snap, &pe)) {
        found = _wcsicmp(pe.szExeFile, L"winamp.exe") == 0;
        if (found && pid) *pid = pe.th32ProcessID;
        if (found && which) {
            wchar_t path[MAX_PATH] = L"?";
            DWORD n = MAX_PATH;
            if (HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pe.th32ProcessID)) {
                QueryFullProcessImageNameW(h, 0, path, &n);
                CloseHandle(h);
            }
            *which = L"PID " + std::to_wstring(pe.th32ProcessID) + L", " + path;
        }
    }
    CloseHandle(snap);
    return found || FindWindowW(L"Winamp v1.x", nullptr) != nullptr || FindWindowW(L"WinampPortable", nullptr) != nullptr;
}

// Asks a Winamp to close as its own Close does (WM_CLOSE to its main window, so it saves its settings).
static void close_winamp(DWORD pid) {
    EnumWindows([](HWND h, LPARAM lp) -> BOOL {
        DWORD p = 0;
        GetWindowThreadProcessId(h, &p);
        wchar_t cls[64] = {};
        GetClassNameW(h, cls, 64);
        if (p == static_cast<DWORD>(lp) && (wcscmp(cls, L"Winamp v1.x") == 0 || wcscmp(cls, L"WinampPortable") == 0))
            PostMessageW(h, WM_CLOSE, 0, 0);
        return TRUE;
    }, static_cast<LPARAM>(pid));
}

// Waits (up to 10 minutes) for a running Winamp to be closed (with --close-running, asks it to close
// first); false if it stays.
static bool wait_for_no_winamp() {
    mark("waiting for no Winamp");
    std::wstring which;
    DWORD pid = 0;
    if (!winamp_running(&which, &pid)) return true;
    if (g_close_running) {
        fwprintf(stderr, L"Winamp is running (%ls): closing it...\n", which.c_str());
        close_winamp(pid);
    } else {
        fwprintf(stderr, L"Winamp is running (%ls): close it to continue (waiting)...\n", which.c_str());
    }
    for (int i = 0; i < 600; ++i) {
        Sleep(1000);
        if (!winamp_running()) {
            Sleep(500); // let it finish writing its settings
            mark("no Winamp");
            return true;
        }
    }
    fwprintf(stderr, L"Winamp still running: stopped\n");
    return false;
}

static BOOL CALLBACK enum_proc(HWND h, LPARAM lp) {
    auto* p = reinterpret_cast<std::pair<DWORD, std::vector<Found>*>*>(lp);
    DWORD pid = 0;
    GetWindowThreadProcessId(h, &pid);
    if (pid != p->first || !IsWindowVisible(h)) return TRUE;
    RECT r;
    GetWindowRect(h, &r);
    if (r.right - r.left < 2 || r.bottom - r.top < 2) return TRUE;
    // Winamp keeps its classic main window "visible" but fully transparent while a modern skin runs.
    BYTE alpha = 255;
    DWORD flags = 0;
    if ((GetWindowLongW(h, GWL_EXSTYLE) & WS_EX_LAYERED) && GetLayeredWindowAttributes(h, nullptr, &alpha, &flags) && (flags & LWA_ALPHA) && alpha == 0)
        return TRUE;
    p->second->push_back({h, r});
    return TRUE;
}

static BOOL CALLBACK child_text_proc(HWND h, LPARAM lp) {
    std::wstring t = window_text(h);
    for (auto& c : t)
        if (c == L'\r' || c == L'\n' || c == L'\t') c = L' ';
    if (!t.empty()) *reinterpret_cast<std::wstring*>(lp) += L" | " + t;
    return TRUE;
}

// --modes: the click plan (capture_util.h). Each entry: a fresh Winamp, the steps, then that window captured in
// its new layout as M<n>_<title>_<layout>_WxH.png, listed in modes.txt ("id<TAB>layout<TAB>png").
static std::map<std::wstring, std::vector<PlanEntry>> g_plan; // by skin file stem
static std::set<std::wstring> g_about; // the plan's "#about<TAB>skin" lines: skins declaring skin.about.group
static std::map<std::wstring, int> g_settle; // the plan's "#settle" lines: the wait after the start, by skin stem
static std::map<std::wstring, int> g_settle_tracks; // "#settle_tracks": the wait after the tracks state is set up
static std::map<std::wstring, std::vector<PlanStep>> g_vis_off; // "#vis_off": the clicks that turn the visualizer off
static std::map<std::wstring, int> g_tracks_pos;    // "#tracks_pos": where the tracks state is paused (ms; else 0:05)

// Added to the plan's waits (after the start and after a click): they're the harness's simulated time, Winamp runs
// behind it (loading the skin's images and scripts in real time); a capture 0.7 s later caught The_HP_Winamp_Skin
// and The_Official_Grind settled, where the plain wait caught them still changing.
// The harness and foobar2000's clock follow the capture's own time ("t="), so clocks aren't affected.
constexpr int kRealTimeMargin = 700;

// The wait after the start: the plan's measure for the skin (+ kRealTimeMargin), else --wait.
static int start_wait(const fs::path& skin_src, int wait_ms) {
    const auto it = g_settle.find(skin_src.stem().wstring());
    return it != g_settle.end() ? it->second + kRealTimeMargin : wait_ms;
}
static std::map<std::wstring, std::vector<std::string>> g_windows; // plan's "#container" lines: names by skin stem

// Every declared window of the skin open at start: Wasabi's saved "activated" (and "everloaded") windows in
// studio.xnf, keyed by window name, set to 1 for each.
static void open_all(const fs::path& settings, const fs::path& skin_src) {
    const auto it = g_windows.find(skin_src.stem().wstring());
    if (it == g_windows.end()) return;
    const fs::path xnf = settings / L"studio.xnf";
    std::ifstream in(xnf, std::ios::binary);
    std::string d((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    in.close();
    auto esc = [](const std::string& s) {
        std::string r;
        for (char c : s) r += c == '&' ? "&amp;" : c == '<' ? "&lt;" : c == '>' ? "&gt;" : c == '"' ? "&quot;" : std::string(1, c);
        return r;
    };
    for (const char* section : {"activated", "everloaded"}) {
        const std::string open = std::string("<section name=\"") + section + "\">";
        size_t a = d.find(open);
        if (a == std::string::npos) {
            fprintf(stderr, "%s: studio.xnf has no %s section: windows not opened\n", utf8(skin_src.filename().wstring()).c_str(), section);
            continue;
        }
        for (const std::string& name : it->second) {
            const size_t end = d.find("</section>", a);
            const std::string entry = "<entry name=\"" + esc(name) + "\" value=\"";
            const size_t e = d.find(entry, a);
            if (e != std::string::npos && e < end) {
                const size_t v = e + entry.size(), q = d.find('"', v);
                d.replace(v, q - v, "1");
            } else {
                const size_t line = d.rfind('\n', end) + 1; // before the closing tag's indentation
                d.insert(line, "        " + entry + "1\" />\r\n");
            }
        }
    }
    std::ofstream o(xnf, std::ios::binary | std::ios::trunc);
    o << d;
}

// A global Wasabi setting in studio.xnf (the "{00000000-...}" section): key="value", replaced or added.
static void set_xnf_value(const fs::path& xnf, const std::string& key, const std::string& value) {
    std::ifstream in(xnf, std::ios::binary);
    std::string d((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    in.close();
    const std::string sec = "<section name=\"{00000000-0000-0000-0000-000000000000}\">", entry = "<entry name=\"" + key + "\" value=\"";
    const size_t s = d.find(sec);
    if (s == std::string::npos) return;
    if (const size_t e = d.find(entry, s); e != std::string::npos) {
        const size_t v = e + entry.size();
        d.replace(v, d.find('"', v) - v, value);
    } else {
        const size_t line = d.find('\n', s) + 1;
        d.insert(line, "      " + entry + value + "\" />\r\n");
    }
    std::ofstream o(xnf, std::ios::binary | std::ios::trunc);
    o << d;
}

// studio.xnf without the windows Wasabi saved per skin ('<section name="Skin:...">': positions, sizes, layouts):
// every skin starts with its own defaults, as foobar2000's test profiles start without our saved skin state.
static void clear_skin_state(const fs::path& xnf) {
    std::ifstream in(xnf, std::ios::binary);
    std::string d((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    in.close();
    const std::string open = "<section name=\"Skin:", sec = "<section", close = "</section>";
    for (size_t s; (s = d.find(open)) != std::string::npos;) {
        size_t p = s + open.size();
        int depth = 1;
        while (depth > 0) {
            const size_t o = d.find(sec, p), c = d.find(close, p);
            if (c == std::string::npos) return; // malformed: leave it
            if (o != std::string::npos && o < c) {
                ++depth;
                p = o + sec.size();
            } else {
                --depth;
                p = c + close.size();
            }
        }
        const size_t from = d.rfind('\n', s) + 1, to = d.find('\n', p);
        d.erase(from, (to == std::string::npos ? d.size() : to + 1) - from);
    }
    std::ofstream o(xnf, std::ios::binary | std::ios::trunc);
    o << d;
}

// The fresh Settings for a capture: every declared window of the skin open, Winamp's own Media Library, Video and
// AVS closed (they aren't tested, and foobar2000 doesn't open its elements for them at start: the skin's buttons
// for them are lit the same way in both), the title standing still (foobar2000's tests: the same).
// tracks: for the tracks state (the visualizer off).
static void prepare_settings(const fs::path& settings, const fs::path& skin_src, bool tracks = false) {
    clear_skin_state(settings / L"studio.xnf");
    open_all(settings, skin_src);
    set_ini_value(settings / L"Plugins" / L"gen_ml.ini", "gen_ml_config", "visible", "0");
    set_winamp_value(settings / L"Winamp.ini", "video_open", "0");
    set_winamp_value(settings / L"Winamp.ini", "visplugin_autoexec", "0");
    set_winamp_value(settings / L"Winamp.ini", "autoscrollname", "0");
    // The visualizer (foobar2000's test profiles the same): the spectrum without tracks, off with them: starting a
    // track it plays a loading animation that the paused tracks state freezes at any frame. Modern skins follow
    // Wasabi's own setting, classic ones Winamp.ini's sa.
    set_xnf_value(settings / L"studio.xnf", "Spectrum analyzer mode", tracks ? "0" : "1");
    set_winamp_value(settings / L"Winamp.ini", "sa", tracks ? "0" : "1");
}

// Winamp's start-up prompts that would block it (e.g. ml_pmp asking whether a removable drive is a portable
// player): answered No. Internet Explorer's "Script Error" dialogs (a skin's browser object showing a page whose
// scripts fail, Shark_by_HH's): closed, as they'd cover the skin's windows in the captures. Called before every capture.
static void dismiss_prompts(DWORD pid) {
    std::vector<Found> wins;
    std::pair<DWORD, std::vector<Found>*> ctx{pid, &wins};
    EnumWindows(enum_proc, reinterpret_cast<LPARAM>(&ctx));
    bool dismissed = false;
    for (const auto& f : wins) {
        wchar_t cls[64] = {};
        GetClassNameW(f.hwnd, cls, 64);
        if (wcscmp(cls, L"Internet Explorer_TridentDlgFrame") == 0) {
            PostMessageW(f.hwnd, WM_CLOSE, 0, 0);
            dismissed = true;
            continue;
        }
        if (wcscmp(cls, L"#32770") != 0 || window_text(f.hwnd) != L"Winamp Portable Music Player Support") continue;
        if (HWND no = GetDlgItem(f.hwnd, IDNO)) SendMessageW(no, BM_CLICK, 0, 0);
        else PostMessageW(f.hwnd, WM_CLOSE, 0, 0);
        dismissed = true;
    }
    if (dismissed) Sleep(300);
}

static HWND find_window(DWORD pid, const std::wstring& id, const std::wstring& name);

// After Winamp's start: its skin window shown (the skin's scripts start then), then the skin's settle time.
static void wait_started(PROCESS_INFORMATION& pi, const fs::path& skin_src, int wait_ms) {
    mark("started");
    WaitForInputIdle(pi.hProcess, 10000);
    mark("input idle");
    for (int i = 0; i < 300 && !find_window(pi.dwProcessId, L"main", L""); ++i) {
        dismiss_prompts(pi.dwProcessId);
        Sleep(50);
    }
    mark("skin window shown");
    Sleep(static_cast<DWORD>(start_wait(skin_src, wait_ms)));
    dismiss_prompts(pi.dwProcessId);
    mark("settled");
}

// Settings restored, the skin selected, Winamp started and given the skin's settle time (start_wait).
static bool launch(const fs::path& winamp, const fs::path& skin_src, int wait_ms, PROCESS_INFORMATION& pi, bool tracks = false) {
    if (!wait_for_no_winamp()) return false;
    const fs::path settings = winamp / L"Settings", clean = winamp / L"Settings.clean";
    std::error_code ec;
    fs::remove_all(settings, ec);
    fs::copy(clean, settings, fs::copy_options::recursive, ec);
    if (ec) { fwprintf(stderr, L"%ls: can't restore Settings\n", skin_src.filename().c_str()); return false; }
    prepare_settings(settings, skin_src, tracks);
    const fs::path skin_dst = winamp / L"Skins" / skin_src.filename();
    if (!fs::equivalent(skin_src, skin_dst, ec)) fs::copy_file(skin_src, skin_dst, fs::copy_options::overwrite_existing, ec);
    if (!set_skin(settings / L"Winamp.ini", skin_src.filename().wstring())) {
        fwprintf(stderr, L"%ls: can't edit Winamp.ini\n", skin_src.filename().c_str());
        return false;
    }
    std::wstring cmd = L"\"" + (winamp / L"winamp.exe").wstring() + L"\"";
    park_cursor();
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    pi = {};
    if (!CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, winamp.c_str(), &si, &pi)) {
        fwprintf(stderr, L"%ls: can't start Winamp\n", skin_src.filename().c_str());
        return false;
    }
    wait_started(pi, skin_src, wait_ms);
    return true;
}

static void quit(PROCESS_INFORMATION& pi) {
    mark("quit");
    TerminateProcess(pi.hProcess, 0);
    WaitForSingleObject(pi.hProcess, 10000);
    mark("quit done");
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
}

// The window of a container: the main one is Winamp's (classic main window, or the skin window titled with
// Winamp's version), the others are titled with their name.
static HWND find_window(DWORD pid, const std::wstring& id, const std::wstring& name) {
    std::vector<Found> wins;
    std::pair<DWORD, std::vector<Found>*> ctx{pid, &wins};
    EnumWindows(enum_proc, reinterpret_cast<LPARAM>(&ctx));
    for (const auto& f : wins) {
        wchar_t cls[64] = {};
        GetClassNameW(f.hwnd, cls, 64);
        const std::wstring title = window_text(f.hwnd);
        if (_wcsicmp(id.c_str(), L"main") == 0) {
            if (wcscmp(cls, L"Winamp v1.x") == 0 || wcscmp(cls, L"WinampPortable") == 0) return f.hwnd;
            // its title: "Winamp <version>", or "<song> - Winamp" (" - Winamp" after a layout switch without one)
            const bool winamp_title = title.rfind(L"Winamp", 0) == 0 ||
                                      (title.size() >= 9 && title.compare(title.size() - 9, 9, L" - Winamp") == 0);
            if (wcscmp(cls, L"BaseWindow_RootWnd") == 0 && winamp_title) return f.hwnd;
        } else if (title == name) {
            return f.hwnd;
        }
    }
    return nullptr;
}

static void run_modes(const fs::path& winamp, const fs::path& skin_src, const fs::path& out, int wait_ms) {
    const auto it = g_plan.find(skin_src.stem().wstring());
    if (it == g_plan.end()) return;
    std::ofstream log(out / L"modes.txt", std::ios::binary | std::ios::trunc);
    int n = 0;
    for (const auto& m : it->second) {
        PROCESS_INFORMATION pi;
        if (!launch(winamp, skin_src, wait_ms, pi)) return;
        bool ok = true;
        for (const PlanStep& st : m.steps) {
            if (st.kind == L"open" && find_window(pi.dwProcessId, st.target, st.target_name)) continue; // open already
            HWND w = find_window(pi.dwProcessId, st.id, st.name);
            if (!w) {
                fwprintf(stderr, L"%ls: no window for %ls\n", skin_src.filename().c_str(), st.name.c_str());
                ok = false;
                break;
            }
            const bool last = &st == &m.steps.back();
            // last step: the entry's window, which must change (shown, or another window or size); else the window the
            // next step clicks, which must be there
            const PlanStep* next = last ? nullptr : &st + 1;
            const std::wstring& id = last ? m.id : next->id;
            const std::wstring& name = last ? m.name : next->name;
            const HWND before = find_window(pi.dwProcessId, id, name);
            RECT rb{};
            if (before) GetWindowRect(before, &rb);
            post_click(w, st);
            // the window the click leads to changed first (Winamp rebuilds a window to switch its layout), then
            // the step's settle time
            for (int i = 0; i < 60; ++i) { // 3 s at most
                const HWND now = find_window(pi.dwProcessId, id, name);
                RECT rn{};
                if (now) GetWindowRect(now, &rn);
                if (now && (!last || now != before || rn.right - rn.left != rb.right - rb.left || rn.bottom - rn.top != rb.bottom - rb.top)) break;
                Sleep(50);
            }
            // then its size steady for 100 ms (Winamp resizes a window in steps, e.g. to shade it)
            for (int i = 0, steady = 0; i < 60 && steady < 2; ++i) {
                const HWND a = find_window(pi.dwProcessId, id, name);
                RECT ra{}, rb2{};
                if (a) GetWindowRect(a, &ra);
                Sleep(50);
                const HWND b = find_window(pi.dwProcessId, id, name);
                if (b) GetWindowRect(b, &rb2);
                steady = a == b && EqualRect(&ra, &rb2) ? steady + 1 : 0;
            }
            Sleep(static_cast<DWORD>(st.settle_ms + kRealTimeMargin));
        }
        if (ok) dismiss_prompts(pi.dwProcessId);
        if (HWND now = ok ? find_window(pi.dwProcessId, m.id, m.name) : nullptr) {
            bring_to_front(now);
            RECT r;
            GetWindowRect(now, &r);
            const std::wstring title = window_text(now);
            wchar_t name[200];
            swprintf(name, 200, L"M%d_%ls_%ls_%ldx%ld.png", n, sanitize(title).c_str(), sanitize(m.layout).c_str(), r.right - r.left, r.bottom - r.top);
            bool opaque = false;
            const std::string at = local_time_ms();
            if (capture_transparent(now, r, (out / name).wstring(), opaque))
                log << utf8(m.id) << '\t' << utf8(m.layout) << '\t' << utf8(name) << "\tt=" << at << "\r\n";
            ++n;
        } else if (ok) {
            fwprintf(stderr, L"%ls: %ls didn't open\n", skin_src.filename().c_str(), m.name.c_str());
        }
        quit(pi);
    }
    printf("%s: %d modes\n", utf8(skin_src.filename().wstring()).c_str(), n);
}

// --- the "tracks" state (--tracks <folder>): as foo_skintest gives foobar2000 -----------------------------

static std::wstring g_tracks; // folder of test tracks

// Winamp's main window (hidden or not; a portable build's class is WinampPortable).
static HWND winamp_main(DWORD pid) {
    std::pair<DWORD, HWND> m{pid, nullptr};
    EnumWindows([](HWND h, LPARAM lp) -> BOOL {
        auto* mm = reinterpret_cast<std::pair<DWORD, HWND>*>(lp);
        DWORD p = 0;
        GetWindowThreadProcessId(h, &p);
        wchar_t c[64] = {};
        GetClassNameW(h, c, 64);
        if (p == mm->first && (wcscmp(c, L"Winamp v1.x") == 0 || wcscmp(c, L"WinampPortable") == 0)) { mm->second = h; return FALSE; }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&m));
    return m.second;
}

// The classic playlist editor's row height: IPC_PE_GETIDXFROMPOINT (WM_USER, wParam 109, lParam a POINT in
// Winamp's memory) asked down the window until the row index changes.
static int pe_row_height(HWND pe, DWORD pid) {
    HANDLE proc = OpenProcess(PROCESS_VM_OPERATION | PROCESS_VM_WRITE, FALSE, pid);
    if (!proc) return 0;
    void* mem = VirtualAllocEx(proc, nullptr, sizeof(POINT), MEM_COMMIT, PAGE_READWRITE);
    int h = 0;
    if (mem) {
        for (int dy = 1; dy < 60 && !h; ++dy) {
            const POINT p{20, 20 + dy};
            WriteProcessMemory(proc, mem, &p, sizeof p, nullptr);
            if (SendMessageW(pe, WM_USER, 109, reinterpret_cast<LPARAM>(mem)) >= 1) h = dy;
        }
        VirtualFreeEx(proc, mem, 0, MEM_RELEASE);
    }
    CloseHandle(proc);
    return h;
}

static void run_tracks(const fs::path& winamp, const fs::path& skin_src, const fs::path& out, int wait_ms) {
    if (g_tracks.empty()) return;
    std::vector<std::wstring> files;
    for (const auto& f : fs::directory_iterator(g_tracks))
        if (_wcsicmp(f.path().extension().c_str(), L".mp3") == 0) files.push_back(f.path().wstring());
    std::sort(files.begin(), files.end());
    if (files.size() < 4) { fwprintf(stderr, L"fewer than 4 .mp3 files in %ls\n", g_tracks.c_str()); return; }
    files.resize(4);
    // the visualizer off: by the plan's clicks where it has them (started on, as the start state: a skin's own
    // mode, mode="1" or a script's, gives way only to a change at run time), else started off
    const auto vo = g_vis_off.find(skin_src.stem().wstring());
    PROCESS_INFORMATION pi;
    if (!launch(winamp, skin_src, wait_ms, pi, vo == g_vis_off.end())) return;
    HWND main = winamp_main(pi.dwProcessId);
    if (!main) { quit(pi); return; }
    const bool classic = _wcsicmp(skin_src.extension().c_str(), L".wal") != 0;
    if (vo != g_vis_off.end()) Sleep(static_cast<DWORD>(vis_off_extra_ms(start_wait(skin_src, wait_ms))));
    if (vo != g_vis_off.end())
        for (const PlanStep& st : vo->second) {
            HWND w = find_window(pi.dwProcessId, st.id, st.name);
            if (!w) { fwprintf(stderr, L"%ls: no window for %ls (visualizer off)\n", skin_src.filename().c_str(), st.name.c_str()); break; }
            post_click(w, st);
            Sleep(static_cast<DWORD>(st.settle_ms + kRealTimeMargin));
        }
    // four tracks, the third playing, paused (straight away: barely a sound) at 0:05
    SendMessageW(main, WM_USER, 0, 101); // IPC_DELETE
    for (const auto& f : files) {
        const std::string a = utf8(f);
        COPYDATASTRUCT cds{100, DWORD(a.size() + 1), const_cast<char*>(a.c_str())}; // IPC_PLAYFILE
        SendMessageW(main, WM_COPYDATA, 0, reinterpret_cast<LPARAM>(&cds));
    }
    for (int i = 0; i < 250 && SendMessageW(main, WM_USER, 0, 124) < 4; ++i) Sleep(20); // IPC_GETLISTLENGTH: added
    for (int i = 0; i < 50 && SendMessageW(main, WM_USER, 0, 125) != 2; ++i) { // IPC_GETLISTPOS
        SendMessageW(main, WM_USER, 2, 121);                                   // IPC_SETPLAYLISTPOS
        Sleep(20);
    }
    SendMessageW(main, WM_COMMAND, 40045, 0); // Play (IPC_STARTPLAY starts from the top of a new list)
    for (int i = 0; i < 250 && SendMessageW(main, WM_USER, 0, 104) != 1; ++i) Sleep(20); // IPC_ISPLAYING
    // the decoder's stream info in (IPC_GETINFO bitrate), and two of Winamp's 100 ms display ticks after it: its
    // first update draws the play icon and the unpaused title (main_timer.cpp g_need_infoupd), over a pause sent
    // before it (classic skins captured "playing" while paused)
    for (int i = 0; i < 150 && SendMessageW(main, WM_USER, 1, 126) == 0; ++i) Sleep(20);
    Sleep(250);
    SendMessageW(main, WM_COMMAND, 40046, 0); // pause
    const ULONGLONG t_pause = GetTickCount64();
    const auto tp = g_tracks_pos.find(skin_src.stem().wstring());
    const int pos_ms = tp != g_tracks_pos.end() ? tp->second : 5000;
    SendMessageW(main, WM_USER, pos_ms, 106);   // IPC_JUMPTOTIME
    for (int i = 0; i < 100 && std::abs(int(SendMessageW(main, WM_USER, 0, 105)) - pos_ms) > 300; ++i) Sleep(20); // IPC_GETOUTPUTTIME: there
    printf("  tracks: playing entry %d, state %d (3: paused), at %d ms\n", int(SendMessageW(main, WM_USER, 0, 125)), int(SendMessageW(main, WM_USER, 0, 104)),
           int(SendMessageW(main, WM_USER, 0, 105)));
    // Winamp's playlist editor: the first track selected (click), the second focused but not (ctrl+click twice).
    // Its window from IPC_GETWND (IPC_GETWND_PE): with a modern skin it's a child of the skin's frame, not a
    // top-level window. It keeps its classic geometry there (the list from y 20: IPC_PE_GETIDXFROMPOINT).
    std::vector<Found> wins;
    std::pair<DWORD, std::vector<Found>*> ctx{pi.dwProcessId, &wins};
    struct Pe { HWND hwnd; };
    std::vector<Pe> pes;
    if (HWND pe = reinterpret_cast<HWND>(SendMessageW(main, WM_USER, 1, 260)); pe && IsWindow(pe)) pes.push_back({pe});
    else fwprintf(stderr, L"  tracks: no playlist editor window\n");
    for (const auto& f : pes) {
        const int h = pe_row_height(f.hwnd, pi.dwProcessId);
        if (h <= 0) { fwprintf(stderr, L"  tracks: no playlist row height\n"); break; }
        auto click = [&](int row, WPARAM keys) {
            const LPARAM at = MAKELPARAM(40, 22 + row * h + h / 2);
            SendMessageW(f.hwnd, WM_MOUSEMOVE, keys, at); // sent: handled when it returns
            SendMessageW(f.hwnd, WM_LBUTTONDOWN, MK_LBUTTON | keys, at);
            SendMessageW(f.hwnd, WM_LBUTTONUP, keys, at);
            Sleep(20);
        };
        click(0, 0);
        click(1, MK_CONTROL);
        click(1, MK_CONTROL);
    }
    if (HWND m2 = find_window(pi.dwProcessId, L"main", L"")) bring_to_front(m2);
    dismiss_prompts(pi.dwProcessId);
    // modern skins: the skin's settle time for this state, plus one poll of Wasabi's SeekBar (sseeker.cpp: it shows
    // the thumb and moves it on a 500 ms timer, not on the change itself; foobar2000's is immediate). Classic skins:
    // the paused time display caught as it switches to shown (it blinks), watched straight away: its second
    // appearance after the pause (a settle wait first would let it pass)
    const auto st = g_settle_tracks.find(skin_src.stem().wstring());
    if (!classic) Sleep(static_cast<DWORD>((st != g_settle_tracks.end() ? st->second : wait_ms) + 500));
    if (HWND m3 = classic ? find_window(pi.dwProcessId, L"main", L"") : nullptr) {
        const ULONGLONG t_watch = GetTickCount64() - t_pause;
        std::string trace;
        if (!wait_time_shown(m3, 4000, &trace)) fwprintf(stderr, L"  tracks: the time display didn't blink\n");
        printf("  tracks: time display shown %llu ms after the pause (watched from %llu:%s)\n", GetTickCount64() - t_pause, t_watch, trace.c_str());
    }
    wins.clear();
    EnumWindows(enum_proc, reinterpret_cast<LPARAM>(&ctx));
    std::ofstream log(out / L"tracks.txt", std::ios::binary | std::ios::trunc);
    RECT wa{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
    char wa_line[96];
    snprintf(wa_line, sizeof wa_line, "#workarea\t%ld,%ld,%ld,%ld\r\n", wa.left, wa.top, wa.right, wa.bottom);
    log << wa_line;
    int n = 0;
    for (const auto& f : wins) {
        wchar_t cls[256] = {};
        GetClassNameW(f.hwnd, cls, 256);
        const std::wstring title = window_text(f.hwnd);
        const int w = f.rect.right - f.rect.left, h = f.rect.bottom - f.rect.top;
        wchar_t name[160];
        swprintf(name, 160, L"T%02d_%ls_%dx%d.png", n, sanitize(title.empty() ? cls : title).c_str(), w, h);
        bool opaque = false;
        const bool skip = untested_window(title);
        const std::string at = local_time_ms();
        const bool ok = !skip && capture_transparent(f.hwnd, f.rect, (out / name).wstring(), opaque);
        char line[1024];
        snprintf(line, sizeof line, "%02d\t%s\t%s\t%ld,%ld %dx%d\tex=%08lx\t%s%s\tt=%s\r\n", n, utf8(cls).c_str(), utf8(title).c_str(), f.rect.left,
                 f.rect.top, w, h, static_cast<unsigned long>(GetWindowLongW(f.hwnd, GWL_EXSTYLE)), skip ? "skipped" : ok ? "saved" : "failed",
                 opaque ? " (opaque)" : "", at.c_str());
        log << line;
        ++n;
    }
    printf("%s: tracks state, %d windows\n", utf8(skin_src.filename().wstring()).c_str(), n);
    // classic skins: the playlist editor windowshaded in this state too (its title and time), as modes.txt's
    // "tracks:pledit<TAB>shade"
    if (classic && !pes.empty() && shade_window(pes[0].hwnd)) {
        RECT r;
        GetWindowRect(pes[0].hwnd, &r);
        wchar_t name[200];
        swprintf(name, 200, L"TM0_%ls_shade_%ldx%ld.png", sanitize(window_text(pes[0].hwnd)).c_str(), r.right - r.left, r.bottom - r.top);
        bool opaque = false;
        const std::string at = local_time_ms();
        if (capture_transparent(pes[0].hwnd, r, (out / name).wstring(), opaque)) {
            std::ofstream ml(out / L"modes.txt", std::ios::binary | std::ios::app);
            ml << "tracks:pledit\tshade\t" << utf8(name) << "\tt=" << at << "\r\n";
        }
    } else if (classic) {
        fwprintf(stderr, L"  tracks: the playlist editor didn't windowshade\n");
    }
    quit(pi);
}

// Descendants of a window, depth first.
static void descendants(HWND parent, std::vector<HWND>& out) {
    for (HWND c = GetWindow(parent, GW_CHILD); c; c = GetWindow(c, GW_HWNDNEXT)) {
        out.push_back(c);
        descendants(c, out);
    }
}

static std::wstring class_of(HWND h) {
    wchar_t cls[64] = {};
    GetClassNameW(h, cls, 64);
    return cls;
}

// The skin's About page (gen_ff's "Current skin" tab, which hosts skin.about.group when the skin declares
// it, else shows the skin info as text): Preferences opened, each tree item selected until gen_ff's page
// (its 5-tab control) shows, its last tab picked, and the Wasabi window in it saved as about_WxH.png.
static void capture_about(DWORD pid, const fs::path& out) {
    // Winamp's main window (hidden while a modern skin runs)
    std::pair<DWORD, HWND> m{pid, nullptr};
    EnumWindows([](HWND h, LPARAM lp) -> BOOL {
        auto* mm = reinterpret_cast<std::pair<DWORD, HWND>*>(lp);
        DWORD p = 0;
        GetWindowThreadProcessId(h, &p);
        const std::wstring c = class_of(h); // a portable build's is WinampPortable
        if (p == mm->first && (c == L"Winamp v1.x" || c == L"WinampPortable")) { mm->second = h; return FALSE; }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&m));
    HWND main = m.second;
    if (!main) { fwprintf(stderr, L"  about: no Winamp main window\n"); return; }
    PostMessageW(main, WM_COMMAND, 40012, 0); // WINAMP_OPTIONS_PREFS
    HWND prefs = nullptr, tree = nullptr;
    for (int i = 0; i < 50 && !tree; ++i) {
        Sleep(100);
        std::vector<Found> wins;
        std::pair<DWORD, std::vector<Found>*> ctx{pid, &wins};
        EnumWindows(enum_proc, reinterpret_cast<LPARAM>(&ctx));
        for (const auto& f : wins) {
            if (class_of(f.hwnd) != L"#32770") continue; // the Preferences dialog, not the library's tree
            std::vector<HWND> kids;
            descendants(f.hwnd, kids);
            for (HWND k : kids)
                if (class_of(k) == L"SysTreeView32") { prefs = f.hwnd; tree = k; break; }
            if (tree) break;
        }
    }
    if (!tree) { fwprintf(stderr, L"  no Preferences window\n"); return; }
    // every item, depth first (handles only: nothing to read from Winamp's memory)
    std::vector<HTREEITEM> items;
    std::vector<HTREEITEM> stack{reinterpret_cast<HTREEITEM>(SendMessageW(tree, TVM_GETNEXTITEM, TVGN_ROOT, 0))};
    while (!stack.empty() && items.size() < 300) {
        HTREEITEM it = stack.back();
        stack.pop_back();
        for (; it; it = reinterpret_cast<HTREEITEM>(SendMessageW(tree, TVM_GETNEXTITEM, TVGN_NEXT, reinterpret_cast<LPARAM>(it)))) {
            items.push_back(it);
            SendMessageW(tree, TVM_EXPAND, TVE_EXPAND, reinterpret_cast<LPARAM>(it));
            if (auto child = reinterpret_cast<HTREEITEM>(SendMessageW(tree, TVM_GETNEXTITEM, TVGN_CHILD, reinterpret_cast<LPARAM>(it))))
                stack.push_back(child);
        }
    }
    bool page = false, shown = false;
    for (HTREEITEM it : items) {
        SendMessageW(tree, TVM_SELECTITEM, TVGN_CARET, reinterpret_cast<LPARAM>(it));
        Sleep(150);
        std::vector<HWND> kids;
        descendants(prefs, kids);
        HWND tab = nullptr;
        for (HWND k : kids)
            if (IsWindowVisible(k) && class_of(k) == L"SysTabControl32" && SendMessageW(k, TCM_GETITEMCOUNT, 0, 0) == 5) tab = k;
        if (!tab) continue;
        page = true;
        SendMessageW(tab, TCM_SETCURFOCUS, 4, 0); // "Current skin" (selects and notifies)
        Sleep(1000);
        kids.clear();
        descendants(prefs, kids);
        for (HWND k : kids)
            if (IsWindowVisible(k) && class_of(k) == L"BaseWindow_RootWnd") {
                RECT r;
                GetWindowRect(k, &r);
                wchar_t name[64];
                swprintf(name, 64, L"about_%ldx%ld.png", r.right - r.left, r.bottom - r.top);
                bool from_screen = false;
                if (capture(k, r, (out / name).wstring(), from_screen)) printf("  about page %ldx%ld\n", r.right - r.left, r.bottom - r.top);
                shown = true;
                break;
            }
        break;
    }
    if (!page) fwprintf(stderr, L"  about: Modern Skins page not found among %zu Preferences pages\n", items.size());
    else if (!shown) printf("  about: the skin has no about group\n");
    PostMessageW(prefs, WM_CLOSE, 0, 0);
}

static bool run_skin(const fs::path& winamp, const fs::path& skin_src, const fs::path& out_root, int wait_ms) {
    if (!wait_for_no_winamp()) return false;
    const fs::path settings = winamp / L"Settings", clean = winamp / L"Settings.clean";
    std::error_code ec;
    fs::remove_all(settings, ec);
    fs::copy(clean, settings, fs::copy_options::recursive, ec);
    if (ec) { fwprintf(stderr, L"%ls: can't restore Settings\n", skin_src.filename().c_str()); return true; }
    prepare_settings(settings, skin_src);

    const fs::path skin_dst = winamp / L"Skins" / skin_src.filename();
    if (!fs::equivalent(skin_src, skin_dst, ec)) fs::copy_file(skin_src, skin_dst, fs::copy_options::overwrite_existing, ec);
    if (!set_skin(settings / L"Winamp.ini", skin_src.filename().wstring())) {
        fwprintf(stderr, L"%ls: can't edit Winamp.ini\n", skin_src.filename().c_str());
        return true;
    }

    const fs::path out = out_root / skin_src.stem();
    fs::create_directories(out, ec);

    std::wstring cmd = L"\"" + (winamp / L"winamp.exe").wstring() + L"\"";
    park_cursor();
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, winamp.c_str(), &si, &pi)) {
        fwprintf(stderr, L"%ls: can't start Winamp\n", skin_src.filename().c_str());
        return true;
    }
    wait_started(pi, skin_src, wait_ms);
    dismiss_prompts(pi.dwProcessId);
    if (HWND main = find_window(pi.dwProcessId, L"main", L"")) bring_to_front(main); // drawn active, as in fb2k_capture

    std::vector<Found> wins;
    std::pair<DWORD, std::vector<Found>*> ctx{pi.dwProcessId, &wins};
    EnumWindows(enum_proc, reinterpret_cast<LPARAM>(&ctx));

    if (wins.empty()) { // another Winamp took the start-up (single instance): keep the earlier capture
        fwprintf(stderr, L"%ls: no windows (is another Winamp running?); earlier capture kept\n", skin_src.filename().c_str());
        TerminateProcess(pi.hProcess, 0);
        WaitForSingleObject(pi.hProcess, 10000);
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        return true;
    }
    // a new capture replaces the old one: its PNGs (numbered by window order, which may differ) and modes.txt
    for (const auto& f : fs::directory_iterator(out, ec))
        if (f.path().extension() == L".png" || f.path().filename() == L"modes.txt" || f.path().filename() == L"tracks.txt") fs::remove(f.path(), ec);
    std::ofstream log(out / L"windows.txt", std::ios::binary | std::ios::trunc);
    RECT wa{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
    char wa_line[96];
    snprintf(wa_line, sizeof wa_line, "#workarea\t%ld,%ld,%ld,%ld\r\n", wa.left, wa.top, wa.right, wa.bottom);
    log << wa_line;
    int n = 0;
    for (const auto& f : wins) {
        wchar_t cls[256] = {};
        GetClassNameW(f.hwnd, cls, 256);
        const std::wstring title = window_text(f.hwnd);
        const int w = f.rect.right - f.rect.left, h = f.rect.bottom - f.rect.top;
        wchar_t name[160];
        swprintf(name, 160, L"%02d_%ls_%dx%d.png", n, sanitize(title.empty() ? cls : title).c_str(), w, h);
        bool opaque = false;
        const bool skip = untested_window(title);
        const std::string at = local_time_ms();
        const bool ok = !skip && capture_transparent(f.hwnd, f.rect, (out / name).wstring(), opaque);
        std::wstring children;
        EnumChildWindows(f.hwnd, child_text_proc, reinterpret_cast<LPARAM>(&children));
        char line[1024];
        snprintf(line, sizeof line, "%02d\t%s\t%s\t%ld,%ld %dx%d\tex=%08lx\t%s%s\tt=%s\t%s\r\n", n, utf8(cls).c_str(), utf8(title).c_str(),
                 f.rect.left, f.rect.top, w, h, static_cast<unsigned long>(GetWindowLongW(f.hwnd, GWL_EXSTYLE)),
                 skip ? "skipped" : ok ? "saved" : "failed", opaque ? " (opaque)" : "", at.c_str(), utf8(children).c_str());
        log << line;
        ++n;
    }
    printf("%s: %d windows\n", utf8(skin_src.filename().wstring()).c_str(), n);
    mark("captured");
    if (g_about.count(skin_src.stem().wstring())) capture_about(pi.dwProcessId, out); // only where the skin has one

    TerminateProcess(pi.hProcess, 0);
    WaitForSingleObject(pi.hProcess, 10000);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    mark("closed");
    run_modes(winamp, skin_src, out, wait_ms);
    mark("modes done");
    run_tracks(winamp, skin_src, out, wait_ms);
    mark("tracks done");
    return true;
}

int wmain(int argc, wchar_t** argv) {
    if (argc < 3) {
        fwprintf(stderr, L"usage: winamp_capture <winamp_dir> <out_dir> [--wait secs] [--list file] [--modes plan.txt] [--tracks folder] [--close-running] [skin.wal ...]\n");
        return 2;
    }
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const fs::path winamp = argv[1], out = argv[2];
    int wait_ms = 1000; // without a plan's measure
    std::vector<fs::path> skins;
    for (int i = 3; i < argc; ++i) {
        const std::wstring a = argv[i];
        if (a == L"--wait" && i + 1 < argc) {
            wait_ms = static_cast<int>(_wtof(argv[++i]) * 1000);
        } else if (a == L"--modes" && i + 1 < argc) {
            g_plan = read_plan(argv[++i]);
            g_settle = read_settle(argv[i]);
            g_settle_tracks = read_settle(argv[i], "#settle_tracks");
            g_tracks_pos = read_settle(argv[i], "#tracks_pos");
            g_vis_off = read_vis_off(argv[i]);
            std::ifstream pl{fs::path(argv[i])};
            for (std::string ln; std::getline(pl, ln);) {
                if (!ln.empty() && ln.back() == '\r') ln.pop_back();
                if (ln.rfind("#about\t", 0) == 0) g_about.insert(widen(ln.substr(7)));
                if (ln.rfind("#container\t", 0) != 0) continue;
                const std::vector<std::wstring> f = split_w(widen(ln), L'\t');
                if (f.size() >= 4 && !f[3].empty()) g_windows[f[1]].push_back(utf8(f[3])); // unnamed: not keyable
            }
        } else if (a == L"--tracks" && i + 1 < argc) {
            g_tracks = fs::absolute(argv[++i]).wstring();
        } else if (a == L"--close-running") {
            g_close_running = true;
        } else if (a == L"--list" && i + 1 < argc) {
            std::wifstream l{fs::path(argv[++i])};
            for (std::wstring s; std::getline(l, s);) {
                while (!s.empty() && (s.back() == L'\r' || s.back() == L' ')) s.pop_back();
                if (!s.empty()) skins.emplace_back(s);
            }
        } else {
            skins.emplace_back(a);
        }
    }
    if (!fs::exists(winamp / L"paths.ini")) {
        fwprintf(stderr, L"%ls has no paths.ini; refusing to touch a non-portable Winamp\n", winamp.c_str());
        return 2;
    }
    std::error_code ec;
    // The Settings as they are now (after a running Winamp has saved them), put back at the end.
    if (!wait_for_no_winamp()) return 1;
    const fs::path kept = winamp / L"Settings.before-capture";
    fs::remove_all(kept, ec);
    fs::copy(winamp / L"Settings", kept, fs::copy_options::recursive, ec);
    if (ec) { fwprintf(stderr, L"can't save Settings.before-capture\n"); return 1; }
    if (!fs::exists(winamp / L"Settings.clean")) {
        fs::copy(winamp / L"Settings", winamp / L"Settings.clean", fs::copy_options::recursive, ec);
        if (ec) { fwprintf(stderr, L"can't save Settings.clean\n"); return 1; }
        printf("saved current Settings as Settings.clean\n");
    }
    fs::create_directories(out, ec);
    for (const auto& s : skins)
        if (!run_skin(winamp, fs::absolute(s), out, wait_ms)) break;
    // Leave the last skin's state out of the user's next normal run: the Settings found at the start.
    fs::remove_all(winamp / L"Settings", ec);
    fs::copy(kept, winamp / L"Settings", fs::copy_options::recursive, ec);
    if (!ec) fs::remove_all(kept, ec);
    return 0;
}
