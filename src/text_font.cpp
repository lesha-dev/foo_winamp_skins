#include "stdafx.h"
#include "text_font.h"

namespace wsz {
namespace {

struct Glyph { int8_t row, col; };

// Glyph positions in text.bmp, as used by Winamp 2.x.
Glyph lookup(wchar_t ch) {
    if (ch >= L'A' && ch <= L'Z') ch = static_cast<wchar_t>(ch - L'A' + L'a');
    if (ch >= L'a' && ch <= L'z') return { 0, static_cast<int8_t>(ch - L'a') };
    if (ch >= L'0' && ch <= L'9') return { 1, static_cast<int8_t>(ch - L'0') };
    switch (ch) {
    case L'"': return { 0, 26 };
    case L'@': return { 0, 27 };
    case L'\x2026': return { 1, 10 }; // ellipsis
    case L'_': return { 1, 11 };
    case L':': return { 1, 12 };
    case L'(': return { 1, 13 };
    case L')': return { 1, 14 };
    case L'-': return { 1, 15 };
    case L'\'': case L'`': case L'\x2019': return { 1, 16 };
    case L'!': return { 1, 17 };
    case L'+': return { 1, 19 };
    case L'\\': return { 1, 20 };
    case L'/': return { 1, 21 };
    case L'[': case L'<': case L'{': return { 1, 22 };
    case L']': case L'>': case L'}': return { 1, 23 };
    case L'^': return { 1, 24 };
    case L'&': return { 1, 25 };
    case L'%': return { 1, 26 };
    case L'.': case L',': return { 1, 27 };
    case L'=': return { 1, 28 };
    case L'$': return { 1, 29 };
    case L'#': return { 1, 30 };
    case L'\xC5': case L'\xE5': return { 2, 0 };  // Å
    case L'\xD6': case L'\xF6': return { 2, 1 };  // Ö
    case L'\xC4': case L'\xE4': return { 2, 2 };  // Ä
    case L'?': return { 2, 3 };
    case L'*': return { 2, 4 };
    }
    return { 0, 30 }; // space
}

} // namespace

void draw_text(Canvas& c, const Image& font, int x, int y, const char* utf8) {
    pfc::stringcvt::string_wide_from_utf8 w(utf8);
    for (const wchar_t* p = w.get_ptr(); *p; ++p, x += font_char_w) {
        const Glyph g = lookup(*p);
        c.blit(font, g.col * font_char_w, g.row * font_char_h, font_char_w, font_char_h, x, y);
    }
}

size_t text_length(const char* utf8) {
    return pfc::stringcvt::string_wide_from_utf8(utf8).length();
}

} // namespace wsz
