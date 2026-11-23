#pragma once

#include <string>

namespace wsz {

// One tooltip per skinned window; its text follows the control under the mouse (set() on mouse move).
// Delays match Winamp's. Hidden entirely when the "tooltips" option is off.
class Tooltip {
public:
    ~Tooltip() { destroy(); }
    void attach(HWND owner);
    void destroy();
    void set(const wchar_t* text); // null/empty: no tooltip here

private:
    HWND m_tip = nullptr;
    HWND m_owner = nullptr;
    std::wstring m_text;
};

} // namespace wsz
