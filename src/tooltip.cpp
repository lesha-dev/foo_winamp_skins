#include "stdafx.h"
#include "tooltip.h"
#include "config.h"

#include <commctrl.h>

namespace wsz {
namespace {

TOOLINFOW tool(HWND owner, wchar_t* text) {
    TOOLINFOW ti = {};
    ti.cbSize = sizeof(ti);
    ti.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
    ti.hwnd = owner;
    ti.uId = reinterpret_cast<UINT_PTR>(owner);
    ti.lpszText = text;
    return ti;
}

} // namespace

void Tooltip::attach(HWND owner) {
    destroy();
    m_owner = owner;
    m_tip = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr, WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX,
        CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, owner, nullptr, core_api::get_my_instance(), nullptr);
    if (!m_tip) return;
    m_text.clear();
    TOOLINFOW ti = tool(owner, const_cast<wchar_t*>(L""));
    SendMessageW(m_tip, TTM_ADDTOOLW, 0, reinterpret_cast<LPARAM>(&ti));
    SendMessageW(m_tip, TTM_SETDELAYTIME, TTDT_INITIAL, 125);
    SendMessageW(m_tip, TTM_SETDELAYTIME, TTDT_AUTOPOP, 2000);
    SendMessageW(m_tip, TTM_SETDELAYTIME, TTDT_RESHOW, 1000);
    SendMessageW(m_tip, TTM_ACTIVATE, FALSE, 0);
}

void Tooltip::destroy() {
    if (m_tip) DestroyWindow(m_tip);
    m_tip = nullptr;
    m_owner = nullptr;
    m_text.clear();
}

void Tooltip::set(const wchar_t* text) {
    if (!m_tip) return;
    const std::wstring t = (text && config::tooltips.get()) ? text : L"";
    if (t == m_text) return;
    m_text = t;
    SendMessageW(m_tip, TTM_POP, 0, 0);
    TOOLINFOW ti = tool(m_owner, m_text.data());
    SendMessageW(m_tip, TTM_UPDATETIPTEXTW, 0, reinterpret_cast<LPARAM>(&ti));
    SendMessageW(m_tip, TTM_ACTIVATE, m_text.empty() ? FALSE : TRUE, 0);
}

} // namespace wsz
