#pragma once

#include <algorithm>
#include <windows.h>

namespace wsz::dpi {

// foobar2000 is system DPI aware, so this is the system DPI; per-monitor DPI where a window gets one
// (WM_DPICHANGED). Null window: the system DPI.
inline UINT window_dpi(HWND wnd) {
    using ForWindow = UINT(WINAPI*)(HWND);
    using ForSystem = UINT(WINAPI*)();
    static const HMODULE user32 = GetModuleHandleW(L"user32.dll");
    static const auto for_window = reinterpret_cast<ForWindow>(GetProcAddress(user32, "GetDpiForWindow"));
    static const auto for_system = reinterpret_cast<ForSystem>(GetProcAddress(user32, "GetDpiForSystem"));
    UINT dpi = 0;
    if (wnd && for_window) dpi = for_window(wnd);
    if (!dpi && for_system) dpi = for_system();
    if (!dpi) {
        HDC dc = GetDC(nullptr);
        dpi = dc ? static_cast<UINT>(GetDeviceCaps(dc, LOGPIXELSY)) : 0;
        if (dc) ReleaseDC(nullptr, dc);
    }
    return dpi ? dpi : USER_DEFAULT_SCREEN_DPI;
}

// Skin pixels are scaled by whole numbers only (nearest neighbour), so the nearest one to the DPI:
// 100-125% 1x, 150-225% 2x, 250-325% 3x, 350-425% 4x...
inline int skin_scale(UINT dpi) { return std::max(1, static_cast<int>((dpi + 48) / 96)); }
inline int skin_scale(HWND wnd) { return skin_scale(window_dpi(wnd)); }

} // namespace wsz::dpi
