// Shared by the capture tools (winamp_capture, fb2k_capture): window capture to PNG and string helpers.
#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <algorithm>
#include <cstring>
#include <fstream>
#include <functional>
#include <map>
#include <string>
#include <vector>

#ifndef PW_RENDERFULLCONTENT
#define PW_RENDERFULLCONTENT 0x00000002
#endif

using Microsoft::WRL::ComPtr;

inline bool write_png(const std::vector<BYTE>& bgra, int w, int h, const std::wstring& path) {
    ComPtr<IWICImagingFactory> f;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&f)))) return false;
    ComPtr<IWICStream> st;
    ComPtr<IWICBitmapEncoder> enc;
    ComPtr<IWICBitmapFrameEncode> fr;
    if (FAILED(f->CreateStream(&st)) || FAILED(st->InitializeFromFilename(path.c_str(), GENERIC_WRITE))) return false;
    if (FAILED(f->CreateEncoder(GUID_ContainerFormatPng, nullptr, &enc)) || FAILED(enc->Initialize(st.Get(), WICBitmapEncoderNoCache))) return false;
    if (FAILED(enc->CreateNewFrame(&fr, nullptr)) || FAILED(fr->Initialize(nullptr))) return false;
    WICPixelFormatGUID fmt = GUID_WICPixelFormat32bppBGRA;
    fr->SetSize(static_cast<UINT>(w), static_cast<UINT>(h));
    if (FAILED(fr->SetPixelFormat(&fmt)) || fmt != GUID_WICPixelFormat32bppBGRA) return false;
    if (FAILED(fr->WritePixels(static_cast<UINT>(h), static_cast<UINT>(w * 4), static_cast<UINT>(bgra.size()), const_cast<BYTE*>(bgra.data())))) return false;
    return SUCCEEDED(fr->Commit()) && SUCCEEDED(enc->Commit());
}

// The pixels of a window (BGRA, alpha 255): PrintWindow (sees the window alone, even when overlapped); if that
// comes back blank (all black, not a layered window) and allow_screen, the screen pixels under the window rect,
// with the window raised first.
inline std::vector<BYTE> grab(HWND hwnd, const RECT& r, bool& from_screen, bool allow_screen = true) {
    const int w = r.right - r.left, h = r.bottom - r.top;
    std::vector<BYTE> px(static_cast<size_t>(w > 0 ? w : 0) * (h > 0 ? h : 0) * 4);
    from_screen = false;
    if (px.empty()) return px;
    HDC screen = GetDC(nullptr);
    HDC dc = CreateCompatibleDC(screen);
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    void* bits = nullptr;
    HBITMAP bmp = CreateDIBSection(screen, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    HGDIOBJ old = SelectObject(dc, bmp);
    bool blank = true;
    if (PrintWindow(hwnd, dc, PW_RENDERFULLCONTENT)) {
        GdiFlush();
        memcpy(px.data(), bits, px.size());
        // a layered window's transparent pixels come back black, which is right (comparisons composite on black):
        // the screen there would show what's behind it
        blank = !(GetWindowLongW(hwnd, GWL_EXSTYLE) & WS_EX_LAYERED);
        for (size_t i = 0; i < px.size() && blank; i += 4) blank = !(px[i] | px[i + 1] | px[i + 2]);
    }
    if (blank && allow_screen) {
        // on top first (not activated), so the screen shows this window and not one overlapping it
        SetWindowPos(hwnd, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        Sleep(300);
        BitBlt(dc, 0, 0, w, h, screen, r.left, r.top, SRCCOPY | CAPTUREBLT);
        GdiFlush();
        memcpy(px.data(), bits, px.size());
        from_screen = true;
    }
    for (size_t i = 3; i < px.size(); i += 4) px[i] = 255; // GDI leaves alpha undefined
    SelectObject(dc, old);
    DeleteObject(bmp);
    DeleteDC(dc);
    ReleaseDC(nullptr, screen);
    return px;
}

// --- transparent capture ------------------------------------------------------------------------------------

// The backdrop window: one solid colour (GWLP_USERDATA), painted synchronously by grab_transparent.
inline LRESULT CALLBACK backdrop_proc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_ERASEBKGND) return 1;
    if (msg == WM_PAINT) {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(h, &ps);
        HBRUSH b = CreateSolidBrush(static_cast<COLORREF>(GetWindowLongPtrW(h, GWLP_USERDATA)));
        RECT rc;
        GetClientRect(h, &rc);
        FillRect(dc, &rc, b);
        DeleteObject(b);
        EndPaint(h, &ps);
        return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

// Waits until the desktop compositor has shown what was drawn (DwmFlush, loaded at run time; else a pause).
inline void compositor_flush() {
    using Flush = HRESULT(WINAPI*)();
    static Flush flush = [] {
        HMODULE m = LoadLibraryW(L"dwmapi.dll");
        return m ? reinterpret_cast<Flush>(reinterpret_cast<void*>(GetProcAddress(m, "DwmFlush"))) : nullptr;
    }();
    if (!flush || FAILED(flush()) || FAILED(flush())) Sleep(50);
    Sleep(15);
}

inline bool on_screen(const RECT& r) {
    const POINT corners[] = {{r.left, r.top}, {r.right - 1, r.top}, {r.left, r.bottom - 1}, {r.right - 1, r.bottom - 1}};
    for (const POINT& pt : corners)
        if (!MonitorFromPoint(pt, MONITOR_DEFAULTTONULL)) return false;
    return true;
}

inline bool grab_screen(HDC screen, const RECT& r, std::vector<BYTE>& px) {
    const int w = r.right - r.left, h = r.bottom - r.top;
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    void* bits = nullptr;
    HDC dc = CreateCompatibleDC(screen);
    HBITMAP bmp = CreateDIBSection(screen, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!bmp) {
        DeleteDC(dc);
        return false;
    }
    HGDIOBJ old = SelectObject(dc, bmp);
    const bool ok = BitBlt(dc, 0, 0, w, h, screen, r.left, r.top, SRCCOPY | CAPTUREBLT) != 0;
    GdiFlush();
    px.assign(static_cast<const BYTE*>(bits), static_cast<const BYTE*>(bits) + static_cast<size_t>(w) * h * 4);
    SelectObject(dc, old);
    DeleteObject(bmp);
    DeleteDC(dc);
    return ok;
}

// A window as the desktop shows it, with its transparency (BGRA, straight alpha): the screen under its rect taken
// over a black and over a white backdrop window placed right below it. The window (and the popups it owns, which
// stay above it) is made topmost meanwhile, so nothing else covers it. Per pixel: alpha = 255 - (white - black),
// colour = black / alpha. Shapes, holes, per-pixel alpha and edges count, whatever way the program draws them.
// Other windows of the program over the rect (the skin's other windows can be owned by this one, so stay above it,
// and opening all windows can stack them) are cleared meanwhile with an empty window region, not hidden (hiding
// would run the skin's own reactions); keep(h) names the ones that are part of this window (foobar2000's hosted
// elements). Empty when the rect isn't wholly on a monitor (the caller falls back to grab()).
inline std::vector<BYTE> grab_transparent(HWND hwnd, const RECT& r, const std::function<bool(HWND)>& keep = {}) {
    const int w = r.right - r.left, h = r.bottom - r.top;
    if (w <= 0 || h <= 0 || !on_screen(r)) return {};
    static const ATOM cls = [] {
        WNDCLASSW wc{};
        wc.lpfnWndProc = backdrop_proc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = L"skintest_backdrop";
        return RegisterClassW(&wc);
    }();
    // the backdrop 1 px wider on each side: its corner shows only the backdrop, so a shot waits until the screen
    // shows the new colour there (painted and composited), not a fixed time
    const RECT ro{r.left - 1, r.top - 1, r.right + 1, r.bottom + 1};
    const POINT corner{ro.left, ro.top};
    const bool checkable = MonitorFromPoint(corner, MONITOR_DEFAULTTONULL) != nullptr;
    HWND bd = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TOPMOST, MAKEINTATOM(cls), L"", WS_POPUP, ro.left, ro.top, w + 2,
                              h + 2, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!bd) return {};
    struct Cleared {
        HWND h;
        HRGN old; // its own region, or null
    };
    struct Ctx {
        HWND target;
        DWORD pid;
        RECT r;
        const std::function<bool(HWND)>* keep;
        std::vector<Cleared> cleared;
    } ctx{hwnd, 0, ro, &keep, {}};
    GetWindowThreadProcessId(hwnd, &ctx.pid);
    EnumWindows([](HWND h, LPARAM lp) -> BOOL {
        auto& c = *reinterpret_cast<Ctx*>(lp);
        DWORD pid = 0;
        GetWindowThreadProcessId(h, &pid);
        RECT wr, both;
        if (h == c.target || pid != c.pid || !IsWindowVisible(h) || !GetWindowRect(h, &wr) || !IntersectRect(&both, &wr, &c.r)) return TRUE;
        if (*c.keep && (*c.keep)(h)) return TRUE;
        HRGN old = CreateRectRgn(0, 0, 0, 0);
        if (GetWindowRgn(h, old) == ERROR) {
            DeleteObject(old);
            old = nullptr;
        }
        if (SetWindowRgn(h, CreateRectRgn(0, 0, 0, 0), TRUE)) c.cleared.push_back({h, old});
        else if (old) DeleteObject(old);
        return TRUE;
    }, reinterpret_cast<LPARAM>(&ctx));
    const bool was_topmost = (GetWindowLongW(hwnd, GWL_EXSTYLE) & WS_EX_TOPMOST) != 0;
    SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    SetWindowLongPtrW(bd, GWLP_USERDATA, RGB(0, 0, 0));
    SetWindowPos(bd, hwnd, ro.left, ro.top, w + 2, h + 2, SWP_NOACTIVATE | SWP_SHOWWINDOW);
    HDC screen = GetDC(nullptr);
    std::vector<BYTE> black, white;
    auto shot = [&](COLORREF c, std::vector<BYTE>& px) {
        SetWindowLongPtrW(bd, GWLP_USERDATA, c);
        RedrawWindow(bd, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW);
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) DispatchMessageW(&msg);
        compositor_flush();
        for (int i = 0; checkable && i < 50 && (GetPixel(screen, corner.x, corner.y) & 0xFFFFFF) != c; ++i) compositor_flush();
        return grab_screen(screen, r, px);
    };
    const bool ok = shot(RGB(0, 0, 0), black) && shot(RGB(255, 255, 255), white);
    ReleaseDC(nullptr, screen);
    DestroyWindow(bd);
    if (!was_topmost) SetWindowPos(hwnd, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    for (const Cleared& c : ctx.cleared) SetWindowRgn(c.h, c.old, TRUE); // the system owns the region from here
    if (!ok) return {};
    std::vector<BYTE> out(black.size());
    for (size_t i = 0; i < out.size(); i += 4) {
        int d = 0;
        for (int c = 0; c < 3; ++c) d += (std::max)(0, int(white[i + c]) - int(black[i + c]));
        const int a = 255 - (d + 1) / 3;
        for (int c = 0; c < 3; ++c) out[i + c] = a > 0 ? BYTE((std::min)(255, (black[i + c] * 255 + a / 2) / a)) : 0;
        out[i + 3] = BYTE(a);
    }
    return out;
}

// A window to a PNG with its transparency (grab_transparent), else opaque (grab; opaque = true).
inline bool capture_transparent(HWND hwnd, const RECT& r, const std::wstring& path, bool& opaque) {
    std::vector<BYTE> px = grab_transparent(hwnd, r);
    opaque = px.empty();
    if (opaque) {
        bool from_screen = false;
        px = grab(hwnd, r, from_screen);
    }
    return !px.empty() && write_png(px, r.right - r.left, r.bottom - r.top, path);
}

// The local time of day as "HH:MM:SS.mmm": when a screenshot is taken (the capture tools' "t=" fields; foobar2000's
// test clock is set to Winamp's, /winamp_skin_clock:).
inline std::string local_time_ms() {
    SYSTEMTIME st;
    GetLocalTime(&st);
    char b[16];
    snprintf(b, sizeof b, "%02u:%02u:%02u.%03u", st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
    return b;
}

// Copies src (sw x sh) into dst (dw x dh) at x, y, clipped.
inline void paste(std::vector<BYTE>& dst, int dw, int dh, const std::vector<BYTE>& src, int sw, int sh, int x, int y) {
    for (int row = 0; row < sh; ++row) {
        const int dy = y + row;
        if (dy < 0 || dy >= dh) continue;
        const int x0 = x < 0 ? -x : 0, x1 = x + sw > dw ? dw - x : sw;
        if (x1 > x0) memcpy(&dst[(static_cast<size_t>(dy) * dw + x + x0) * 4], &src[(static_cast<size_t>(row) * sw + x0) * 4], static_cast<size_t>(x1 - x0) * 4);
    }
}

// Captures a window to a PNG (see grab).
inline bool capture(HWND hwnd, const RECT& r, const std::wstring& path, bool& from_screen) {
    const std::vector<BYTE> px = grab(hwnd, r, from_screen);
    return !px.empty() && write_png(px, r.right - r.left, r.bottom - r.top, path);
}

// Media Library, Video and AVS (Winamp's own windows, or a skin's frame titled for them) are not tested: such a
// window is listed in windows.txt as "skipped", without a screenshot.
inline bool untested_window(const std::wstring& title) {
    for (const wchar_t* n : {L"Media Library", L"Winamp Library", L"Video", L"Winamp Video", L"AVS", L"Visualization"})
        if (_wcsicmp(title.c_str(), n) == 0) return true;
    return false;
}

inline std::wstring sanitize(std::wstring s) {
    for (auto& c : s)
        if (c < 32 || wcschr(L"\\/:*?\"<>|", c)) c = L'_';
    if (s.size() > 60) s.resize(60);
    return s;
}

inline std::string utf8(const std::wstring& s) {
    std::string r(static_cast<size_t>(WideCharToMultiByte(CP_UTF8, 0, s.c_str(), -1, nullptr, 0, nullptr, nullptr)), '\0');
    WideCharToMultiByte(CP_UTF8, 0, s.c_str(), -1, r.data(), static_cast<int>(r.size()), nullptr, nullptr);
    if (!r.empty()) r.pop_back();
    return r;
}

inline std::wstring window_text(HWND h) {
    wchar_t buf[512] = {};
    GetWindowTextW(h, buf, 512);
    return buf;
}

inline std::wstring widen(const std::string& s) {
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
    return w;
}

// --- the click plan (modern_harness --modes; skintest adds classic skins' lines) ---------------------------
// "skin<TAB>container id<TAB>container name<TAB>layout<TAB>steps", steps "id|name|kind|x|y|target id|target
// name" joined by ";": from a fresh start, click (kind click), double-click (dblclick) or, unless the target
// window is open already, click to open it (open) in the window of that container, then capture the
// container's window in that layout. Lines starting with "#" are comments.
struct PlanStep {
    std::wstring id, name, kind, target, target_name;
    int x = 0, y = 0;
    int settle_ms = 100; // the wait after this click before capturing (the plan's measure)
};
struct PlanEntry {
    std::wstring id, name, layout;
    std::vector<PlanStep> steps;
};

inline std::vector<std::wstring> split_w(const std::wstring& s, wchar_t sep) {
    std::vector<std::wstring> f;
    size_t from = 0;
    for (size_t i; (i = s.find(sep, from)) != std::wstring::npos; from = i + 1) f.push_back(s.substr(from, i - from));
    f.push_back(s.substr(from));
    return f;
}

// by skin file stem
// The plan's "#settle<TAB>skin<TAB>ms" lines: how long after its start each skin's windows settle (modern_harness
// measures it in its engine: until nothing but looping animations changes, plus 0.1 s).
// kind "#settle" (after the start) or "#settle_tracks" (after the tracks state is set up).
inline std::map<std::wstring, int> read_settle(const std::wstring& path, const std::string& kind = "#settle") {
    std::map<std::wstring, int> out;
    std::ifstream in(path, std::ios::binary);
    for (std::string line; std::getline(in, line);) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.rfind(kind + "\t", 0) != 0) continue;
        const std::vector<std::wstring> f = split_w(widen(line), L'\t');
        if (f.size() >= 3 && _wtoi(f[2].c_str()) > 0) out[f[1]] = _wtoi(f[2].c_str());
    }
    return out;
}

// steps "id|name|kind|x|y|target id|target name|settle ms" joined by ";"
inline std::vector<PlanStep> parse_steps(const std::wstring& s) {
    std::vector<PlanStep> out;
    for (const std::wstring& st : split_w(s, L';')) {
        const std::vector<std::wstring> p = split_w(st, L'|');
        if (p.size() < 5) continue;
        out.push_back({p[0], p[1], p[2], p.size() > 5 ? p[5] : L"", p.size() > 6 ? p[6] : L"", _wtoi(p[3].c_str()), _wtoi(p[4].c_str()),
                       p.size() > 7 && _wtoi(p[7].c_str()) > 0 ? _wtoi(p[7].c_str()) : 100});
    }
    return out;
}

// The plan's "#vis_off<TAB>skin<TAB>steps" lines: the clicks that turn the skin's visualizer off, replayed before
// the tracks state is set up (Winamp and foobar2000 then start it with the visualizer on, as the start state).
// Skins without one start the tracks state with it off.
inline std::map<std::wstring, std::vector<PlanStep>> read_vis_off(const std::wstring& path) {
    std::map<std::wstring, std::vector<PlanStep>> out;
    std::ifstream in(path, std::ios::binary);
    for (std::string line; std::getline(in, line);) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.rfind("#vis_off\t", 0) != 0) continue;
        const std::vector<std::wstring> f = split_w(widen(line), L'\t');
        if (f.size() >= 3)
            if (std::vector<PlanStep> s = parse_steps(f[2]); !s.empty()) out[f[1]] = std::move(s);
    }
    return out;
}

inline std::map<std::wstring, std::vector<PlanEntry>> read_plan(const std::wstring& path) {
    std::map<std::wstring, std::vector<PlanEntry>> plan;
    std::ifstream in(path, std::ios::binary);
    for (std::string line; std::getline(in, line);) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#') continue;
        const std::vector<std::wstring> f = split_w(widen(line), L'\t');
        if (f.size() < 5) continue;
        plan[f[0]].push_back(PlanEntry{f[1], f[2], f[3], parse_steps(f[4])});
    }
    return plan;
}

// A click (or double-click) posted to a window at window coordinates.
inline void post_click(HWND w, const PlanStep& s) {
    const LPARAM at = MAKELPARAM(s.x, s.y);
    PostMessageW(w, WM_MOUSEMOVE, 0, at);
    PostMessageW(w, WM_LBUTTONDOWN, MK_LBUTTON, at);
    PostMessageW(w, WM_LBUTTONUP, 0, at);
    if (s.kind == L"dblclick") {
        PostMessageW(w, WM_LBUTTONDBLCLK, MK_LBUTTON, at);
        PostMessageW(w, WM_LBUTTONUP, 0, at);
    }
}

// The mouse pointer away from the skin's windows (the primary monitor's right edge, halfway down: easy to find
// again after a run): one resting on a window shows its tooltip or hover image in the capture.
inline void park_cursor() { SetCursorPos(GetSystemMetrics(SM_CXSCREEN) - 1, GetSystemMetrics(SM_CYSCREEN) / 2); }

// Makes the main window the foreground one, so it's drawn active (title bars) the same way on every run; a
// background process may not take the foreground, so this joins the current foreground thread's input first.
inline bool bring_to_front(HWND h) {
    for (int attempt = 0; attempt < 3 && GetForegroundWindow() != h; ++attempt) {
        const DWORD fg = GetWindowThreadProcessId(GetForegroundWindow(), nullptr), me = GetCurrentThreadId();
        if (fg && fg != me) AttachThreadInput(me, fg, TRUE);
        if (attempt > 0) { // the foreground lock: a key event from this process lifts it
            keybd_event(VK_MENU, 0, 0, 0);
            keybd_event(VK_MENU, 0, KEYEVENTF_KEYUP, 0);
        }
        SetForegroundWindow(h);
        BringWindowToTop(h);
        if (fg && fg != me) AttachThreadInput(me, fg, FALSE);
        for (int i = 0; i < 20 && GetForegroundWindow() != h; ++i) Sleep(50);
    }
    Sleep(100); // repaint
    return GetForegroundWindow() == h;
}

// Classic skins: the time display's area (classic main window 36,26 63x13, BGRA rows) as on screen, the window
// brought to front.
inline std::vector<BYTE> time_area(HWND main) {
    bring_to_front(main);
    RECT wr{};
    GetWindowRect(main, &wr);
    const RECT area{wr.left + 36, wr.top + 26, wr.left + 36 + 63, wr.top + 26 + 13};
    HDC screen = GetDC(nullptr);
    std::vector<BYTE> px;
    grab_screen(screen, area, px);
    ReleaseDC(nullptr, screen);
    return px;
}

// Classic skins, paused: the time display blinks, in Winamp every 1.2 s (main_timer.cpp: 12 ticks of 100 ms, the
// phase flipped by any redraw), in ours every 1 s; neither phase is known from outside. Called right after the
// pause (no settle wait before it), it waits until the display has just switched to shown (its digits drawn): in
// Winamp its second appearance after the pause (hidden again 0.2 s after it), and both programs are caught alike
// with a second to capture in. The area is watched every 20 ms for its two looks; the shown one is told by the four
// digit cells (x 48, 60, 78 and 90, 9x13): the tracks state is paused at 0:0<n> with n > 0, so shown they read
// "00:0n", the last cell standing out from the three equal ones. Its score: pixels differing (a channel off by more
// than 24, as the comparisons count) between the last two cells, less the most between the first three; hidden
// (blank cells, or the background without them) it's lower. Cells are compared with each other rather than to a
// fixed threshold: a numbers bitmap shorter than 13 px leaves the background, unequal, under its digits.
// Returns false if it didn't blink within max_ms.
inline bool wait_time_shown(HWND main, int max_ms = 4000, std::string* trace = nullptr) {
    auto differ = [](const std::vector<BYTE>& px, int xa, int xb) { // digit cells at area x xa and xb
        int n = 0;
        for (int y = 0; y < 13; ++y)
            for (int x = 0; x < 9; ++x) {
                const BYTE* a = &px[(y * 63 + xa + x) * 4];
                const BYTE* b = &px[(y * 63 + xb + x) * 4];
                n += std::abs(a[0] - b[0]) > 24 || std::abs(a[1] - b[1]) > 24 || std::abs(a[2] - b[2]) > 24;
            }
        return n;
    };
    auto score = [&](const std::vector<BYTE>& px) {
        if (px.size() < 63 * 13 * 4) return 0;
        return differ(px, 42, 54) - (std::max)(differ(px, 12, 24), differ(px, 24, 42));
    };
    const ULONGLONG t0 = GetTickCount64(), end = t0 + max_ms;
    auto next_change = [&](const std::vector<BYTE>& from, std::vector<BYTE>& to) {
        while (GetTickCount64() < end) {
            Sleep(20);
            to = time_area(main);
            if (to != from) {
                if (trace) *trace += " +" + std::to_string(GetTickCount64() - t0) + "ms score " + std::to_string(score(from)) + "->" + std::to_string(score(to));
                return true;
            }
        }
        return false;
    };
    std::vector<BYTE> a = time_area(main), b, c;
    bool ok = next_change(a, b);
    if (ok && score(b) < score(a)) ok = next_change(b, c); // just hidden: the next switch shows it
    return ok;
}

// Windowshade by a double-click on a window's title bar (as the plan's classic "shade" entries); waits until its
// size has changed and stayed the same for 100 ms (Winamp resizes a window in steps). False if it didn't change.
inline bool shade_window(HWND w) {
    RECT r0{};
    GetWindowRect(w, &r0);
    post_click(w, PlanStep{L"", L"", L"dblclick", L"", L"", 100, 5});
    auto height = [&] {
        RECT r{};
        GetWindowRect(w, &r);
        return r.bottom - r.top;
    };
    for (int i = 0; i < 60 && height() == r0.bottom - r0.top; ++i) Sleep(50); // 3 s at most
    for (int i = 0, steady = 0; i < 60 && steady < 2; ++i) {
        RECT a{}, b{};
        GetWindowRect(w, &a);
        Sleep(50);
        GetWindowRect(w, &b);
        steady = EqualRect(&a, &b) ? steady + 1 : 0;
    }
    Sleep(100); // repaint
    return height() != r0.bottom - r0.top;
}

// Waits until the windows of process pid that pass is_ours look the same (rects and pixels) for quiet_ms, after
// at least min_ms; returns after max_ms anyway (an animation never settles). Real time; polls every 50 ms.
template <class F> void settle(DWORD pid, F is_ours, int min_ms, int quiet_ms, int max_ms) {
    struct Ctx {
        DWORD pid;
        F* is_ours;
        std::vector<HWND> out;
    } ctx{pid, &is_ours, {}};
    unsigned long long last = 0;
    ULONGLONG stable_since = 0;
    const ULONGLONG start = GetTickCount64();
    for (;;) {
        const ULONGLONG now = GetTickCount64();
        const int t = int(now - start);
        if (t >= max_ms) return;
        ctx.out.clear();
        EnumWindows([](HWND h, LPARAM lp) -> BOOL {
            auto* c = reinterpret_cast<Ctx*>(lp);
            DWORD p = 0;
            GetWindowThreadProcessId(h, &p);
            if (p == c->pid && IsWindowVisible(h) && (*c->is_ours)(h)) c->out.push_back(h);
            return TRUE;
        }, reinterpret_cast<LPARAM>(&ctx));
        unsigned long long sig = 1469598103934665603ull;
        auto mix = [&](unsigned long long v) { sig = (sig ^ v) * 1099511628211ull; };
        for (HWND h : ctx.out) {
            RECT r;
            GetWindowRect(h, &r);
            mix(reinterpret_cast<uintptr_t>(h));
            mix((unsigned long long)(r.left & 0xffff) << 48 | (unsigned long long)(r.top & 0xffff) << 32 | (r.right & 0xffff) << 16 | (r.bottom & 0xffff));
            bool scr = false;
            const std::vector<BYTE> px = grab(h, r, scr, false);
            for (size_t i = 0; i < px.size(); i += 8) mix(*reinterpret_cast<const unsigned long long*>(&px[i]));
        }
        if (ctx.out.empty() || sig != last) stable_since = now;
        last = sig;
        if (t >= min_ms && int(now - stable_since) >= quiet_ms) return;
        Sleep(50);
    }
}

// More wait before the plan's "#vis_off" clicks, on top of the start wait: in real time a long start (an intro
// animation that a script then fades out and hides, The_ATL's spinner) ends later than the harness measures
// (Wasabi's timers run late, the animation starts after the window shows), and while it still covers the
// visualizer, the click goes to it (a Layer takes clicks over its whole rect).
inline int vis_off_extra_ms(int start_wait_ms) { return start_wait_ms / 5; }
