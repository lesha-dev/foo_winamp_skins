#pragma once

#include "classic/canvas.h"

namespace wsz {

// text.bmp bitmap font: 5x6 glyphs, 31 columns x 3 rows.
constexpr int font_char_w = 5;
constexpr int font_char_h = 6;

// Draws UTF-8 text; unsupported characters render as space. Clipping is the caller's job (Canvas::set_clip).
void draw_text(Canvas& c, const Image& font, int x, int y, const char* utf8);

// Number of glyphs draw_text() would render for the string.
size_t text_length(const char* utf8);

// A window whose art the skin lacks (and no fallback skin supplies): "Select fallback skin" centred on its title row,
// the top title_h rows, in a small Windows font (9 px in every window).
void draw_missing_art_note(Canvas& c, int title_h);

} // namespace wsz
