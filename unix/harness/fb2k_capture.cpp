// Development tool (not shipped): runs a portable foobar2000 once per skin and saves the windows of
// foo_winamp_skins as PNGs, for comparison with Winamp's (winamp_capture) and with earlier runs.
//
//   fb2k_capture.exe <fb2k_dir> <profile_template> <out_dir> [--wait secs] [--modes plan.txt] [--tracks folder]
//                    [--tracks-profile template] skin ...
//
// <fb2k_dir> must be a portable install (portable_mode_enabled) used only for tests: before each skin its
// profile folder is replaced by a copy of <profile_template> (which selects the UI, Winamp Skins or Winamp
// Modern Skins, and holds the component under test), then foobar2000 is started with "/winamp_skin:<skin>"
// (and "/winamp_skin_clock:<time>": the time of Winamp's screenshot of the state that start is for, from the
// plan's "#clock" lines, so clocks show Winamp's time with nothing left to animate; 12:00:00 without one)
// and given --wait seconds (default 4). Output per skin: <out_dir>\<skin stem>\NN_<title>_WxH.png for every
// visible window of the component (with the UI elements it hosts drawn in), and windows.txt in winamp_capture's format ("#workarea<TAB>l,t,r,b", then
// "NN<TAB>class<TAB>title<TAB>x,y WxH<TAB>ex=<ex-style><TAB>saved|failed|other"); other visible windows
// of the process (an error popup, foobar2000's own dialogs) are listed as "other" without a PNG. The main window
// is brought to the front first (drawn active); "#foreground<TAB>failed" says that didn't work. Windows are
// captured with PrintWindow only, so other windows on the desktop never show in them. --modes: the click plan
// (capture_util.h), each entry from a fresh start: M<n>_<title>_<layout>_WxH.png and modes.txt ("id<TAB>layout
// <TAB>png"), as winamp_capture. For skins the plan marks "#about" (declaring skin.about.group): Preferences opened on
// the modern UI's Current skin page ("/winamp_skin_prefs:about") and its about area saved as about_WxH.png.
// --tracks: the tracks state, "/skintest_tracks:<folder>" (the foo_skintest component in the profile: four tracks,
// the third paused at 0:05 or the plan's "#tracks_pos", the first selected, the second focused), every window
// saved as T<NN>_*.png and listed in tracks.txt. In the running foobar2000 when no mode capture changed a window
// there (the visualizer turned off and the tracks forwarded to it), else in one more start: from the profile, the
// visualizer turned off by the plan's "#vis_off" clicks (as winamp_capture), or from --tracks-profile (the
// visualizer saved off) for skins without them. The visualizer off in the running foobar2000: those clicks too, else
// "/winamp_skin_vis:off".
// A running foobar2000 from <fb2k_dir> is closed first; others are left alone.

#include "capture_util.h"

#include <tlhelp32.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

struct Found {
    HWND hwnd;
    RECT rect;
};

std::wstring class_of(HWND h) {
    wchar_t cls[128] = {};
    GetClassNameW(h, cls, 128);
    return cls;
}

std::vector<DWORD> processes_of(const fs::path& exe) {
    std::vector<DWORD> out;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return out;
    PROCESSENTRY32W pe{};
    pe.dwSize = sizeof(pe);
    for (BOOL ok = Process32FirstW(snap, &pe); ok; ok = Process32NextW(snap, &pe)) {
        if (_wcsicmp(pe.szExeFile, L"foobar2000.exe") != 0) continue;
        if (HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pe.th32ProcessID)) {
            wchar_t path[MAX_PATH * 2] = {};
            DWORD n = DWORD(std::size(path));
            if (QueryFullProcessImageNameW(h, 0, path, &n)) {
                std::error_code ec;
                if (fs::equivalent(fs::path(path), exe, ec)) out.push_back(pe.th32ProcessID);
            }
            CloseHandle(h);
        }
    }
    CloseHandle(snap);
    return out;
}

// Closes foobar2000 processes of this install: WM_CLOSE to their windows, terminated after 10 s.
bool close_running(const fs::path& exe) {
    const std::vector<DWORD> pids = processes_of(exe);
    for (DWORD pid : pids) {
        fprintf(stderr, "closing the running test foobar2000 (PID %lu)\n", pid);
        EnumWindows([](HWND h, LPARAM lp) -> BOOL {
            DWORD p = 0;
            GetWindowThreadProcessId(h, &p);
            if (p == DWORD(lp) && IsWindowVisible(h)) PostMessageW(h, WM_CLOSE, 0, 0);
            return TRUE;
        }, LPARAM(pid));
        if (HANDLE h = OpenProcess(SYNCHRONIZE | PROCESS_TERMINATE, FALSE, pid)) {
            if (WaitForSingleObject(h, 10000) != WAIT_OBJECT_0) {
                TerminateProcess(h, 1);
                WaitForSingleObject(h, 5000);
            }
            CloseHandle(h);
        }
    }
    return processes_of(exe).empty();
}

BOOL CALLBACK enum_proc(HWND h, LPARAM lp) {
    auto* p = reinterpret_cast<std::pair<DWORD, std::vector<Found>*>*>(lp);
    DWORD pid = 0;
    GetWindowThreadProcessId(h, &pid);
    if (pid != p->first || !IsWindowVisible(h)) return TRUE;
    RECT r;
    GetWindowRect(h, &r);
    if (r.right - r.left < 2 || r.bottom - r.top < 2) return TRUE;
    BYTE alpha = 255;
    DWORD flags = 0;
    if ((GetWindowLongW(h, GWL_EXSTYLE) & WS_EX_LAYERED) && GetLayeredWindowAttributes(h, nullptr, &alpha, &flags) && (flags & LWA_ALPHA) && alpha == 0)
        return TRUE;
    p->second->push_back({h, r});
    return TRUE;
}

// The component's skin windows (classic main/child windows, modern layout windows).
bool is_skin_window(const std::wstring& cls) {
    return cls == L"foo_winamp_skins_main" || cls == L"foo_winamp_skins_child" || cls == L"foo_winamp_skins_modern_layout";
}

// The modern UI's hosts of embedded UI elements (the playlist, a library viewer...) and of the skin's Edit boxes:
// popups owned by their layout window, captured as part of it.
bool is_host_window(const std::wstring& cls) {
    return cls == L"foo_winamp_skins_modern_host" || cls == L"foo_winamp_skins_modern_edit";
}

// A skin window with the host popups it owns drawn in, as on screen, with its transparency (grab_transparent:
// the hosts are above it, so they're in). Off-screen: opaque (opaque = true), PrintWindow only (the screen can
// show anything on top, and a black playlist reads as "blank"), the hosts pasted in.
bool capture_with_hosts(HWND hwnd, const RECT& r, const std::vector<Found>& all, const std::wstring& path, bool& opaque) {
    const int w = r.right - r.left, h = r.bottom - r.top;
    std::vector<BYTE> px = grab_transparent(hwnd, r, [hwnd](HWND h) { return is_host_window(class_of(h)) && GetWindow(h, GW_OWNER) == hwnd; });
    opaque = px.empty();
    if (!opaque) return write_png(px, w, h, path);
    bool from_screen = false;
    px = grab(hwnd, r, from_screen, false);
    if (px.empty()) return false;
    // hosts in z-order bottom to top (EnumWindows lists top first)
    for (auto it = all.rbegin(); it != all.rend(); ++it) {
        if (!is_host_window(class_of(it->hwnd)) || GetWindow(it->hwnd, GW_OWNER) != hwnd) continue;
        bool scr = false;
        const std::vector<BYTE> sub = grab(it->hwnd, it->rect, scr, false);
        paste(px, w, h, sub, it->rect.right - it->rect.left, it->rect.bottom - it->rect.top, it->rect.left - r.left, it->rect.top - r.top);
    }
    return write_png(px, w, h, path);
}

bool copy_profile(const fs::path& from, const fs::path& to) {
    std::error_code ec;
    fs::remove_all(to, ec);
    if (fs::exists(to)) return false;
    fs::copy(from, to, fs::copy_options::recursive, ec);
    return !ec;
}

struct Launched {
    PROCESS_INFORMATION pi{};
    ~Launched() {
        if (!pi.hProcess) return;
        TerminateProcess(pi.hProcess, 0);
        WaitForSingleObject(pi.hProcess, 10000);
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
    }
};

std::map<std::wstring, int> g_settle; // the plan's "#settle" lines: the wait after the start, by skin stem
// The plan's "#clock<TAB>skin<TAB>state<TAB>HH:MM:SS.mmm" lines: when Winamp's screenshot of that state was taken
// (state "start", "tracks" or "<id>:<layout>"), ms since midnight, by "stem|state".
std::map<std::wstring, int> g_clock;
int g_forward_ms = 150; // how long the last forwarded /winamp_skin_clock took

int clock_of(const fs::path& skin, const std::wstring& state) {
    const auto it = g_clock.find(skin.stem().wstring() + L"|" + state);
    return it == g_clock.end() ? -1 : it->second;
}

// Hands command-line tokens to the running foobar2000: a second foobar2000.exe forwards them to it and exits.
void forward(const fs::path& fb2k, const std::wstring& args) {
    std::wstring cmd = L"\"" + (fb2k / L"foobar2000.exe").wstring() + L"\"" + args;
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    if (CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, fb2k.c_str(), &si, &pi)) {
        WaitForSingleObject(pi.hProcess, 10000);
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
    }
}

// Waits wait_ms before a capture, with foobar2000's test clock set (forwarded to the running one) so that it shows
// at (ms since midnight; -1: left alone) when the wait is over: the time Winamp's screenshot shows.
void clock_then_wait(const fs::path& fb2k, int at, int wait_ms) {
    if (at < 0) {
        Sleep(static_cast<DWORD>(wait_ms));
        return;
    }
    const int day = 24 * 3600 * 1000, v = ((at - wait_ms + g_forward_ms) % day + day) % day;
    wchar_t arg[64];
    swprintf(arg, 64, L" \"/winamp_skin_clock:%02d:%02d:%02d.%03d\"", v / 3600000, v / 60000 % 60, v / 1000 % 60, v % 1000);
    const fs::path exe = fb2k / L"foobar2000.exe";
    std::wstring cmd = L"\"" + exe.wstring() + L"\"" + arg;
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    const ULONGLONG t0 = GetTickCount64();
    if (CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, fb2k.c_str(), &si, &pi)) {
        WaitForSingleObject(pi.hProcess, 10000);
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
    }
    g_forward_ms = static_cast<int>(GetTickCount64() - t0);
    if (wait_ms > g_forward_ms) Sleep(static_cast<DWORD>(wait_ms - g_forward_ms));
}

std::map<std::wstring, int> g_settle_tracks; // "#settle_tracks": the wait after the tracks state is set up
std::map<std::wstring, std::vector<PlanStep>> g_vis_off; // "#vis_off": the clicks that turn the visualizer off
std::map<std::wstring, int> g_tracks_pos;    // "#tracks_pos": where the tracks state is paused (ms; else 0:05)

// A fresh profile from the template, foobar2000 started with the skin; after the component says the skin is
// loaded, the skin's settle time (the plan's measure, else --wait).

// state: the capture this start is for ("start", "tracks", "<id>:<layout>"): its clock is the one set.
bool launch(const fs::path& fb2k, const fs::path& tmpl, const fs::path& skin, int wait_ms, Launched& l, const std::wstring& extra = L"",
            const std::wstring& state = L"start") {
    const fs::path exe = fb2k / L"foobar2000.exe";
    if (!close_running(exe)) {
        fprintf(stderr, "the test foobar2000 doesn't close: stopped\n");
        return false;
    }
    if (!copy_profile(tmpl, fb2k / L"profile")) {
        fprintf(stderr, "can't restore the profile from %s\n", utf8(tmpl.wstring()).c_str());
        return false;
    }
    // the clock first: the skin then loads with it. Winamp's time of the capture this start is for, less the start's
    // settle time and a second for loading, so that a clock that animates its changes (FlipClock) has no change left
    // to animate when it's set exactly after the load (clock_then_wait); 12:00 without one
    const auto st = g_settle.find(skin.stem().wstring());
    const int settle_ms = st != g_settle.end() ? st->second : wait_ms;
    std::wstring clock_arg = L" /winamp_skin_clock:12:00:00";
    if (const int at = clock_of(skin, state); at >= 0) {
        const int day = 24 * 3600 * 1000, v = ((at - settle_ms - 1000) % day + day) % day;
        wchar_t arg[64];
        swprintf(arg, 64, L" \"/winamp_skin_clock:%02d:%02d:%02d.%03d\"", v / 3600000, v / 60000 % 60, v / 1000 % 60, v % 1000);
        clock_arg = arg;
    }
    std::wstring cmd = L"\"" + exe.wstring() + L"\"" + clock_arg + L" \"/winamp_skin:" + skin.wstring() + L"\"" + extra;
    park_cursor();
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    if (!CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, fb2k.c_str(), &si, &l.pi)) {
        fprintf(stderr, "can't start %s\n", utf8(exe.wstring()).c_str());
        return false;
    }
    const ULONGLONG t0 = GetTickCount64();
    WaitForInputIdle(l.pi.hProcess, 15000);
    // the component says when the skin is loaded; then until its windows stop changing (wait_ms at most)
    const std::wstring ev_name = L"Local\\foo_winamp_skins_loaded_" + std::to_wstring(l.pi.dwProcessId);
    HANDLE ev = nullptr;
    for (int i = 0; i < 150 && !ev; ++i) {
        ev = OpenEventW(SYNCHRONIZE, FALSE, ev_name.c_str());
        if (!ev) {
            if (WaitForSingleObject(l.pi.hProcess, 0) == WAIT_OBJECT_0) break; // it quit
            Sleep(100);
        }
    }
    if (ev) {
        WaitForSingleObject(ev, 15000);
        CloseHandle(ev);
    } else {
        fprintf(stderr, "no skin-loaded signal (an older foo_winamp_skins?): waiting %d ms\n", wait_ms);
    }
    const ULONGLONG t1 = GetTickCount64();
    // the skin's settle time (the plan's measure, else --wait) after it's loaded
    clock_then_wait(fb2k, clock_of(skin, state), settle_ms);
    if (getenv("FB2K_CAPTURE_TIMES")) printf("  start: loaded after %llu ms, settled after %llu more\n", t1 - t0, GetTickCount64() - t1);
    return true;
}

std::vector<Found> windows_of(DWORD pid) {
    std::vector<Found> wins;
    std::pair<DWORD, std::vector<Found>*> ctx{pid, &wins};
    EnumWindows(enum_proc, reinterpret_cast<LPARAM>(&ctx));
    return wins;
}

bool is_main(HWND h) {
    const std::wstring cls = class_of(h);
    return cls == L"foo_winamp_skins_main" || (cls == L"foo_winamp_skins_modern_layout" && (GetWindowLongW(h, GWL_EXSTYLE) & WS_EX_APPWINDOW));
}

// The window of a container: "main" is the main window, the others are titled with their name.
HWND find_window(DWORD pid, const std::wstring& id, const std::wstring& name) {
    for (const auto& f : windows_of(pid)) {
        if (!is_skin_window(class_of(f.hwnd))) continue;
        if (_wcsicmp(id.c_str(), L"main") == 0 ? is_main(f.hwnd) : window_text(f.hwnd) == name) return f.hwnd;
    }
    return nullptr;
}

// Brings the main window to the front; false (and a "#foreground" line) if another program kept the focus.
bool front(DWORD pid, std::ofstream& log, PROCESS_INFORMATION& pi) {
    for (const auto& f : windows_of(pid))
        if (is_main(f.hwnd)) {
            if (bring_to_front(f.hwnd)) return true;
            break;
        }
    HWND fg = GetForegroundWindow();
    DWORD fpid = 0;
    GetWindowThreadProcessId(fg, &fpid);
    log << "#foreground\tfailed\t" << utf8(class_of(fg)) << "\t" << utf8(window_text(fg)) << "\t"
        << (fpid == pi.dwProcessId ? "same process" : "other process") << "\r\n";
    return false;
}

// The modern UI's Preferences page "Current skin": its about control (IDC_SKIN_ABOUT) saved as about_WxH.png.
void capture_about(const fs::path& exe, DWORD pid, const fs::path& out) {
    std::wstring cmd = L"\"" + exe.wstring() + L"\" /winamp_skin_prefs:about"; // forwarded to the running one
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    if (CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, exe.parent_path().c_str(), &si, &pi)) {
        WaitForSingleObject(pi.hProcess, 10000);
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
    }
    for (int i = 0; i < 100; ++i) {
        Sleep(100);
        for (const auto& f : windows_of(pid)) {
            if (class_of(f.hwnd) != L"#32770") continue;
            std::vector<HWND> kids;
            EnumChildWindows(f.hwnd, [](HWND h, LPARAM lp) -> BOOL { reinterpret_cast<std::vector<HWND>*>(lp)->push_back(h); return TRUE; },
                             reinterpret_cast<LPARAM>(&kids));
            for (HWND k : kids) {
                if (GetDlgCtrlID(k) != 1128 || !IsWindowVisible(k)) continue; // IDC_SKIN_ABOUT
                Sleep(150); // drawn
                // the client area only: the control's border isn't the skin's (Winamp's about group has none)
                RECT r, c;
                GetWindowRect(k, &r);
                GetClientRect(k, &c);
                POINT o{0, 0};
                ClientToScreen(k, &o);
                const int ww = r.right - r.left, cw = c.right, ch = c.bottom, dx = o.x - r.left, dy = o.y - r.top;
                wchar_t name[64];
                swprintf(name, 64, L"about_%dx%d.png", cw, ch);
                bool from_screen = false;
                const std::vector<BYTE> px = grab(k, r, from_screen, false);
                if (!px.empty() && cw > 0 && ch > 0) {
                    std::vector<BYTE> client(static_cast<size_t>(cw) * ch * 4);
                    for (int y = 0; y < ch; ++y)
                        memcpy(&client[static_cast<size_t>(y) * cw * 4], &px[(static_cast<size_t>(y + dy) * ww + dx) * 4], static_cast<size_t>(cw) * 4);
                    write_png(client, cw, ch, (out / name).wstring());
                }
                PostMessageW(f.hwnd, WM_CLOSE, 0, 0);
                for (int j = 0; j < 30 && IsWindow(f.hwnd); ++j) Sleep(100); // gone before the next clicks
                return;
            }
        }
    }
    fprintf(stderr, "  about: no Preferences page\n");
}

// Every visible window of the process: the component's saved as <prefix>NN_<title>_WxH.png (with the hosts of
// embedded elements drawn in), all listed in <log_name> (winamp_capture's windows.txt format); the main window is
// brought to the front first.
void capture_windows(PROCESS_INFORMATION& pi, const fs::path& out, const std::wstring& prefix, const std::wstring& log_name, int& n, int& saved) {
    std::ofstream log(out / log_name, std::ios::binary | std::ios::trunc);
    RECT wa{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
    char wa_line[96];
    snprintf(wa_line, sizeof wa_line, "#workarea\t%ld,%ld,%ld,%ld\r\n", wa.left, wa.top, wa.right, wa.bottom);
    log << wa_line;
    front(pi.dwProcessId, log, pi);
    const std::vector<Found> wins = windows_of(pi.dwProcessId);
    for (const auto& f : wins) {
        const std::wstring cls = class_of(f.hwnd), title = window_text(f.hwnd);
        if (is_host_window(cls)) continue; // in its owner's capture
        const int w = f.rect.right - f.rect.left, h = f.rect.bottom - f.rect.top;
        const char* state = "other";
        bool opaque = false;
        if (is_skin_window(cls) && untested_window(title)) {
            state = "skipped";
        } else if (is_skin_window(cls)) {
            wchar_t name[160];
            swprintf(name, 160, L"%ls%02d_%ls_%dx%d.png", prefix.c_str(), n, sanitize(title.empty() ? cls : title).c_str(), w, h);
            state = capture_with_hosts(f.hwnd, f.rect, wins, (out / name).wstring(), opaque) ? "saved" : "failed";
            saved += state[0] == 's';
        }
        char line[1024];
        snprintf(line, sizeof line, "%02d\t%s\t%s\t%ld,%ld %dx%d\tex=%08lx\t%s%s\r\n", n, utf8(cls).c_str(), utf8(title).c_str(), f.rect.left,
                 f.rect.top, w, h, static_cast<unsigned long>(GetWindowLongW(f.hwnd, GWL_EXSTYLE)), state, opaque ? " (opaque)" : "");
        log << line;
        ++n;
    }
}

std::map<std::wstring, std::vector<PlanEntry>> g_plan; // --modes, by skin file stem
std::wstring g_tracks;                                 // --tracks: folder of test tracks (foo_skintest)
fs::path g_tracks_profile; // --tracks-profile: the profile template for the tracks state (else the main one)
std::set<std::wstring> g_about; // the plan's "#about<TAB>skin" lines: skins declaring skin.about.group

bool run_skin(const fs::path& fb2k, const fs::path& tmpl, const fs::path& skin, const fs::path& out_root, int wait_ms) {
    const fs::path out = out_root / skin.stem();
    std::error_code ec;
    fs::create_directories(out, ec);
    for (const auto& f : fs::directory_iterator(out, ec))
        if (f.path().extension() == L".png" || f.path().filename() == L"modes.txt" || f.path().filename() == L"tracks.txt") fs::remove(f.path(), ec);
    int n = 0, saved = 0, modes = 0, launches = 1;
    const ULONGLONG t0 = GetTickCount64();
    ULONGLONG t_launch = 0, t_about = 0;
    auto l = std::make_unique<Launched>();
    if (!launch(fb2k, tmpl, skin, wait_ms, *l)) return false;
    t_launch = GetTickCount64() - t0;
    {
        capture_windows(l->pi, out, L"", L"windows.txt", n, saved);
        const ULONGLONG ta = GetTickCount64();
        if (g_about.count(skin.stem().wstring())) capture_about(fb2k / L"foobar2000.exe", l->pi.dwProcessId, out);
        t_about = GetTickCount64() - ta;
    }
    // The plan: each entry's clicks, then the window captured in its new layout. Entries run on in the same
    // foobar2000 while they click only windows no earlier entry changed (Winamp's captures start fresh each
    // time; this gives the same state, faster); otherwise a fresh start.
    std::set<std::wstring> dirty; // container ids changed in the running foobar2000
    if (const auto it = g_plan.find(skin.stem().wstring()); it != g_plan.end()) {
        std::ofstream mlog(out / L"modes.txt", std::ios::binary | std::ios::trunc);
        for (const PlanEntry& m : it->second) {
            bool fresh = dirty.count(m.id) > 0;
            for (const PlanStep& st : m.steps) fresh |= dirty.count(st.id) || (!st.target.empty() && dirty.count(st.target));
            if (fresh) {
                l = std::make_unique<Launched>();
                if (!launch(fb2k, tmpl, skin, wait_ms, *l, L"", m.id + L":" + m.layout)) return false;
                dirty.clear();
                ++launches;
            }
            const DWORD pid = l->pi.dwProcessId;
            bool ok = true;
            for (const PlanStep& st : m.steps) {
                if (st.kind == L"open" && find_window(pid, st.target, st.target_name)) continue;
                HWND w = find_window(pid, st.id, st.name);
                if (!w) {
                    fprintf(stderr, "%s: no window %s\n", utf8(skin.filename().wstring()).c_str(), utf8(st.name).c_str());
                    ok = false;
                    break;
                }
                const bool last = &st == &m.steps.back();
                // last step: the entry's window, which must change (shown, or another window or size); else the window the
                // next step clicks, which must be there
                const PlanStep* next = last ? nullptr : &st + 1;
                const std::wstring& id = last ? m.id : next->id;
                const std::wstring& name = last ? m.name : next->name;
                const HWND before = find_window(pid, id, name);
                RECT rb{};
                if (before) GetWindowRect(before, &rb);
                post_click(w, st);
                // the window the click leads to changed first, then the step's settle time
                for (int i = 0; i < 60; ++i) { // 3 s at most
                    const HWND now = find_window(pid, id, name);
                    RECT rn{};
                    if (now) GetWindowRect(now, &rn);
                    if (now && (!last || now != before || rn.right - rn.left != rb.right - rb.left || rn.bottom - rn.top != rb.bottom - rb.top)) break;
                    Sleep(50);
                }
                // then its size steady for 100 ms (Winamp resizes a window in steps, e.g. to shade it)
                for (int i = 0, steady = 0; i < 60 && steady < 2; ++i) {
                    const HWND a = find_window(pid, id, name);
                    RECT ra{}, rb2{};
                    if (a) GetWindowRect(a, &ra);
                    Sleep(50);
                    const HWND b = find_window(pid, id, name);
                    if (b) GetWindowRect(b, &rb2);
                    steady = a == b && EqualRect(&ra, &rb2) ? steady + 1 : 0;
                }
                // the window's capture: the clock as Winamp's screenshot of this layout shows it
                clock_then_wait(fb2k, &st == &m.steps.back() ? clock_of(skin, m.id + L":" + m.layout) : -1, st.settle_ms);
                dirty.insert(st.id);
                if (!st.target.empty()) dirty.insert(st.target);
            }
            dirty.insert(m.id);
            HWND now = ok ? find_window(pid, m.id, m.name) : nullptr;
            if (!now) {
                if (ok) fprintf(stderr, "%s: %s didn't open\n", utf8(skin.filename().wstring()).c_str(), utf8(m.name).c_str());
                continue;
            }
            bring_to_front(now);
            RECT r;
            GetWindowRect(now, &r);
            wchar_t name[200];
            swprintf(name, 200, L"M%d_%ls_%ls_%ldx%ld.png", modes, sanitize(window_text(now)).c_str(), sanitize(m.layout).c_str(), r.right - r.left,
                     r.bottom - r.top);
            bool opaque = false;
            if (capture_with_hosts(now, r, windows_of(pid), (out / name).wstring(), opaque))
                mlog << utf8(m.id) << '\t' << utf8(m.layout) << '\t' << utf8(name) << "\r\n";
            ++modes;
        }
    }
    // With tracks (--tracks): foo_skintest's state, every window again as T<NN>_*.png and tracks.txt.
    int tn = 0, tsaved = 0;
    if (!g_tracks.empty()) {
        const auto tp = g_tracks_pos.find(skin.stem().wstring());
        const std::wstring pos_arg = tp != g_tracks_pos.end() ? L" \"/skintest_pos:" + std::to_wstring(tp->second) + L"\"" : L"";
        const std::wstring tracks_arg = pos_arg + L" \"/skintest_tracks:" + g_tracks + L"\"";
        // the visualizer off by the plan's clicks, as winamp_capture (a skin's own mode gives way only to a change at
        // run time), from the start state
        const auto vo = g_vis_off.find(skin.stem().wstring());
        auto vis_off_clicks = [&] {
            const auto st0 = g_settle.find(skin.stem().wstring());
            Sleep(static_cast<DWORD>(vis_off_extra_ms(st0 != g_settle.end() ? st0->second : wait_ms)));
            for (const PlanStep& st : vo->second) {
                HWND w = find_window(l->pi.dwProcessId, st.id, st.name);
                if (!w) {
                    fprintf(stderr, "%s: no window %s (visualizer off)\n", utf8(skin.filename().wstring()).c_str(), utf8(st.name).c_str());
                    break;
                }
                post_click(w, st);
                clock_then_wait(fb2k, -1, st.settle_ms);
            }
        };
        if (dirty.empty() && l && l->pi.hProcess && WaitForSingleObject(l->pi.hProcess, 0) == WAIT_TIMEOUT) {
            // the running foobar2000 still shows the start (no mode changed a window): the visualizer off, as the
            // tracks profile has it, and the tracks added to it; no second start
            if (vo != g_vis_off.end()) vis_off_clicks();
            else forward(fb2k, L" /winamp_skin_vis:off");
            forward(fb2k, tracks_arg);
        } else if (vo != g_vis_off.end()) {
            l = std::make_unique<Launched>();
            if (!launch(fb2k, tmpl, skin, wait_ms, *l, L"", L"tracks")) return false;
            ++launches;
            vis_off_clicks();
            forward(fb2k, tracks_arg);
        } else {
            l = std::make_unique<Launched>();
            if (!launch(fb2k, g_tracks_profile.empty() ? tmpl : g_tracks_profile, skin, wait_ms, *l, tracks_arg, L"tracks")) return false;
            ++launches;
        }
        const std::wstring ev_name = L"Local\\skintest_tracks_" + std::to_wstring(l->pi.dwProcessId);
        HANDLE ev = nullptr;
        for (int i = 0; i < 100 && !ev; ++i)
            if (!(ev = OpenEventW(SYNCHRONIZE, FALSE, ev_name.c_str()))) Sleep(100);
        if (ev) {
            WaitForSingleObject(ev, 10000); // set once paused and moved
            CloseHandle(ev);
            const ULONGLONG t_pause = GetTickCount64();
            const bool classic = _wcsicmp(skin.extension().c_str(), L".wal") != 0;
            // modern skins: the skin's settle time for this state. Classic skins: the paused time display caught as it
            // switches to shown (it blinks), watched straight away, as winamp_capture does
            const auto st = g_settle_tracks.find(skin.stem().wstring());
            if (!classic) clock_then_wait(fb2k, clock_of(skin, L"tracks"), st != g_settle_tracks.end() ? st->second : wait_ms);
            if (classic)
                for (const auto& f : windows_of(l->pi.dwProcessId))
                    if (class_of(f.hwnd) == L"foo_winamp_skins_main") {
                        const ULONGLONG t_watch = GetTickCount64() - t_pause;
        std::string trace;
        if (!wait_time_shown(f.hwnd, 4000, &trace)) fwprintf(stderr, L"  tracks: the time display didn't blink\n");
                        printf("  tracks: time display shown %llu ms after the pause (watched from %llu:%s)\n", GetTickCount64() - t_pause, t_watch, trace.c_str());
                        break;
                    }
            capture_windows(l->pi, out, L"T", L"tracks.txt", tn, tsaved);
            // classic skins: the playlist editor windowshaded in this state too, as winamp_capture does
            if (HWND pe = classic ? find_window(l->pi.dwProcessId, L"pledit", L"Winamp Playlist Editor") : nullptr; pe && shade_window(pe)) {
                RECT r;
                GetWindowRect(pe, &r);
                wchar_t name[200];
                swprintf(name, 200, L"TM0_%ls_shade_%ldx%ld.png", sanitize(window_text(pe)).c_str(), r.right - r.left, r.bottom - r.top);
                bool opaque = false;
                if (capture_with_hosts(pe, r, windows_of(l->pi.dwProcessId), (out / name).wstring(), opaque)) {
                    std::ofstream ml(out / L"modes.txt", std::ios::binary | std::ios::app);
                    ml << "tracks:pledit\tshade\t" << utf8(name) << "\r\n";
                }
            } else if (classic) {
                fwprintf(stderr, L"  tracks: the playlist editor didn't windowshade\n");
            }
        } else {
            std::ifstream lg(fb2k / L"profile" / L"skintest.log");
            std::string steps, ln;
            while (std::getline(lg, ln)) steps += (steps.empty() ? "" : ", ") + ln;
            fprintf(stderr, "%s: no tracks state (foo_skintest steps: %s)\n", utf8(skin.filename().wstring()).c_str(),
                    steps.empty() ? "none: is it in the profile?" : steps.c_str());
        }
    }
    printf("%s: %d windows, %d saved, %d modes; %llu ms (first start %llu, about %llu, %d starts)\n", utf8(skin.filename().wstring()).c_str(), n,
           saved, modes, GetTickCount64() - t0, t_launch, t_about, launches);
    return true;
}

} // namespace

int wmain(int argc, wchar_t** argv) {
    if (argc < 5) {
        fwprintf(stderr, L"usage: fb2k_capture <fb2k_dir> <profile_template> <out_dir> [--wait secs] [--modes plan.txt] [--tracks folder] [--tracks-profile template] skin ...\n");
        return 2;
    }
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const fs::path fb2k = fs::absolute(argv[1]), tmpl = fs::absolute(argv[2]), out = fs::absolute(argv[3]);
    int wait_ms = 1000; // without a plan's measure
    std::vector<fs::path> skins;
    for (int i = 4; i < argc; ++i) {
        const std::wstring a = argv[i];
        if (a == L"--wait" && i + 1 < argc) wait_ms = int(_wtof(argv[++i]) * 1000);
        else if (a == L"--tracks" && i + 1 < argc) g_tracks = fs::absolute(argv[++i]).wstring();
        else if (a == L"--tracks-profile" && i + 1 < argc) g_tracks_profile = fs::absolute(argv[++i]);
        else if (a == L"--modes" && i + 1 < argc) {
            g_plan = read_plan(argv[++i]);
            g_settle = read_settle(argv[i]);
            g_settle_tracks = read_settle(argv[i], "#settle_tracks");
            g_tracks_pos = read_settle(argv[i], "#tracks_pos");
            g_vis_off = read_vis_off(argv[i]);
            std::ifstream pl{fs::path(argv[i])};
            for (std::string ln; std::getline(pl, ln);) {
                if (!ln.empty() && ln.back() == '\r') ln.pop_back();
                if (ln.rfind("#about\t", 0) == 0) g_about.insert(widen(ln.substr(7)));
                if (ln.rfind("#clock\t", 0) == 0) {
                    const std::vector<std::wstring> f = split_w(widen(ln), L'\t');
                    int h = 0, mi = 0, s = 0, ms = 0;
                    if (f.size() >= 4 && swscanf_s(f[3].c_str(), L"%d:%d:%d.%d", &h, &mi, &s, &ms) == 4)
                        g_clock[f[1] + L"|" + f[2]] = ((h * 60 + mi) * 60 + s) * 1000 + ms;
                }
            }
        }
        else skins.push_back(fs::absolute(a));
    }
    if (!fs::exists(fb2k / L"portable_mode_enabled") || !fs::exists(fb2k / L"foobar2000.exe")) {
        fwprintf(stderr, L"%ls is not a portable foobar2000; refusing to touch it\n", fb2k.c_str());
        return 2;
    }
    if (!fs::is_directory(tmpl)) {
        fwprintf(stderr, L"no profile template %ls\n", tmpl.c_str());
        return 2;
    }
    std::error_code ec;
    fs::create_directories(out, ec);
    for (const auto& s : skins)
        if (!run_skin(fb2k, tmpl, s, out, wait_ms)) return 1;
    return 0;
}
