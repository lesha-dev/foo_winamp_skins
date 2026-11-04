#include "stdafx.h"
#include "gen_frame.h"

namespace wsz {
namespace {

using S = layout::Sprite;

bool has_gen(const Skin& skin) { return !skin.bitmap(SkinBitmap::gen).empty(); }

// Nearest-neighbour stretch, used where Winamp stretches the last partial tile.
void stretch(Canvas& c, const Image& img, S s, int dx, int dy, int dw, int dh) {
    if (dw <= 0 || dh <= 0) return;
    for (int y = 0; y < dh; ++y) {
        const int sy = s.y + y * s.h / dh;
        if (sy < 0 || sy >= img.height) continue;
        for (int x = 0; x < dw; ++x) {
            const int sx = s.x + x * s.w / dw;
            if (sx < 0 || sx >= img.width) continue;
            c.set_pixel(dx + x, dy + y, img.pixels[size_t(sy) * img.width + sx]);
        }
    }
}

// Title font in gen.bmp: letters A-Z on one 7 px row, separated by the background colour found at x=0.
struct Letter { int x = 0, w = 0; };
std::array<Letter, 26> scan_letters(const Image& img, int y) {
    std::array<Letter, 26> out{};
    if (y >= img.height) return out;
    const uint32_t* row = &img.pixels[size_t(y) * img.width];
    const uint32_t bg = row[0];
    int x = 1;
    for (auto& l : out) {
        int n = x;
        while (n < img.width && row[n] != bg) ++n;
        l = { x, n - x };
        x = n + 1;
    }
    return out;
}

constexpr int unknown_char_w = 5;

void render_gen(Canvas& c, const Image& g, int w, int h, const char* title, bool active, bool close_pressed) {
    const int sy = active ? 0 : 21;
    const int text_y = active ? 88 : 96;
    const auto letters = scan_letters(g, text_y);
    auto width_of = [&](char ch) {
        if (ch >= 'a' && ch <= 'z') ch = static_cast<char>(ch - 'a' + 'A');
        return ch >= 'A' && ch <= 'Z' && letters[ch - 'A'].w ? letters[ch - 'A'].w : unknown_char_w;
    };
    int text_exact = 0;
    for (const char* p = title; *p; ++p) text_exact += width_of(*p);

    // Title bar: corner, filler, title section in 25 px steps, filler, corner (with close button).
    int text_w = text_exact + 24;
    text_w -= text_w % 25;
    text_w = std::min(text_w, w - 100);
    const int fill_n = (w - 100 - text_w) / 25;
    int x = 0;
    c.blit(g, 0, sy, 25, 20, x, 0); x += 25;
    if (fill_n & 1) { c.blit(g, 104, sy, 12, 20, x, 0); x += 12; }
    for (int i = 0; i < fill_n / 2; ++i) { c.blit(g, 104, sy, 25, 20, x, 0); x += 25; }
    c.blit(g, 26, sy, 25, 20, x, 0); x += 25;
    if (text_w >= 25) {
        int tx = x + (text_w - text_exact) / 2 + (text_w != text_exact ? 1 : 0);
        for (int i = 0; i < text_w / 25; ++i) { c.blit(g, 52, sy, 25, 20, x, 0); x += 25; }
        int room = text_w;
        for (const char* p = title; *p; ++p) {
            char ch = *p;
            if (ch >= 'a' && ch <= 'z') ch = static_cast<char>(ch - 'a' + 'A');
            const int cw = width_of(ch);
            if (cw > room) break;
            if (ch >= 'A' && ch <= 'Z' && letters[ch - 'A'].w) c.blit(g, letters[ch - 'A'].x, text_y, cw, 7, tx, 4);
            tx += cw;
            room -= cw;
        }
    }
    c.blit(g, 78, sy, 25, 20, x, 0); x += 25;
    if (fill_n & 1) { c.blit(g, 104, sy, 13, 20, x, 0); x += 13; }
    for (int i = 0; i < fill_n / 2; ++i) { c.blit(g, 104, sy, 25, 20, x, 0); x += 25; }
    const int rest = (w - 100 - text_w) % 25;
    if (rest > 0) { stretch(c, g, { 104, sy, 25, 20 }, x, 0, rest, 20); x += rest; }
    c.blit(g, 130, sy, 25, 20, x, 0);
    if (close_pressed) c.blit(g, 148, 42, 9, 9, w - 11, 3);

    // Sides: 29 px tiles, the last one stretched, then the 24 px lower pieces.
    int y = 20;
    for (int i = 0; i < (h - 58) / 29; ++i) {
        c.blit(g, 127, 42, 11, 29, 0, y);
        c.blit(g, 139, 42, 8, 29, w - 8, y);
        y += 29;
    }
    if (const int r = (h - 58) % 29) {
        stretch(c, g, { 127, 42, 11, 29 }, 0, y, 11, r);
        stretch(c, g, { 139, 42, 8, 29 }, w - 8, y, 8, r);
        y += r;
    }
    c.blit(g, 158, 42, 11, 24, 0, y);
    c.blit(g, 170, 42, 8, 24, w - 8, y);
    y += 24;

    // Bottom: two 125 px ends with 25 px filler between.
    c.blit(g, 0, 42, 125, 14, 0, y);
    x = 125;
    for (int i = 0; i < (w - 250) / 25; ++i) { c.blit(g, 127, 72, 25, 14, x, y); x += 25; }
    if (const int r = (w - 250) % 25) { stretch(c, g, { 127, 72, 25, 14 }, x, y, r, 14); x += r; }
    c.blit(g, 0, 57, 125, 14, x, y);
}

// pledit.bmp pieces (same sprites the playlist editor uses).
constexpr S pl_top_tile{ 127, 21, 25, 20 }, pl_top_tile_sel{ 127, 0, 25, 20 };
constexpr S pl_top_left{ 0, 21, 25, 20 }, pl_top_left_sel{ 0, 0, 25, 20 };
constexpr S pl_top_right{ 153, 21, 25, 20 }, pl_top_right_sel{ 153, 0, 25, 20 };
constexpr S pl_left_tile{ 0, 42, 12, 29 }, pl_right_tile{ 31, 42, 20, 29 };
constexpr S pl_bottom_tile{ 179, 0, 25, 38 };
constexpr S pl_close_down{ 52, 42, 9, 9 };
constexpr int pl_top_h = 20, pl_bottom_h = 38, pl_left_w = 12, pl_right_w = 20;

void render_pledit(Canvas& c, const Image& img, int w, int h, bool active, bool close_pressed) {
    c.tile(img, active ? pl_top_tile_sel : pl_top_tile, 25, 0, w - 50, pl_top_h);
    c.blit(img, active ? pl_top_left_sel : pl_top_left, { 0, 0 });
    c.blit(img, active ? pl_top_right_sel : pl_top_right, { w - 25, 0 });
    if (close_pressed) c.blit(img, pl_close_down, { w - 11, 3 });
    c.tile(img, pl_left_tile, 0, pl_top_h, pl_left_w, h - pl_top_h - pl_bottom_h);
    c.tile(img, pl_right_tile, w - pl_right_w, pl_top_h, pl_right_w, h - pl_top_h - pl_bottom_h);
    c.tile(img, pl_bottom_tile, 0, h - pl_bottom_h, w, pl_bottom_h);
}

} // namespace

GenFrame::Insets GenFrame::insets(const Skin& skin) {
    if (has_gen(skin)) return { 11, 20, 8, 14 };
    return { pl_left_w, pl_top_h, pl_right_w, pl_bottom_h };
}

void GenFrame::render(Canvas& c, const Skin& skin, int w, int h, const char* title, bool active, bool close_pressed) {
    c.clear(0);
    if (has_gen(skin)) render_gen(c, skin.bitmap(SkinBitmap::gen), w, h, title, active, close_pressed);
    else render_pledit(c, skin.bitmap(SkinBitmap::pledit), w, h, active, close_pressed);
}

} // namespace wsz
