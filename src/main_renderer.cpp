#include "stdafx.h"
#include "main_renderer.h"
#include "text_font.h"

namespace wsz {
namespace L = layout;

namespace {

int frame_for(double t) { // t in 0..1; Winamp truncates
    return std::clamp(static_cast<int>(t * (L::slider_frames - 1) + 1e-9), 0, L::slider_frames - 1);
}

L::Sprite down(L::Sprite s, int dy) { return { s.x, s.y + dy, s.w, s.h }; }

void draw_slider(Canvas& c, const Image& img, L::Sprite bg, L::Sprite thumb, L::Sprite thumb_down, bool pressed,
                 L::Pos pos, double frame_t, double thumb_t, int travel) {
    const int f = frame_for(frame_t);
    c.blit(img, bg.x, f * L::slider_frame_step, bg.w, L::slider_frame_h, pos.x, pos.y);
    // Skins without a thumb row just show the background. A partial row (e.g. 430px tall) is drawn
    // clipped, like Winamp does.
    if (img.height > thumb.y) {
        const int x = pos.x + static_cast<int>(std::clamp(thumb_t, 0.0, 1.0) * travel + 1e-9);
        c.blit(img, pressed ? thumb_down : thumb, { x, pos.y + L::slider_thumb_dy });
    }
}

// Unpressed buttons come from the title bar background itself (active or inactive row), like Winamp.
// The skin draws an easter egg title bar: both rows present and not a single flat colour (left empty).
bool has_easter_egg(const Image& title) {
    const auto s = L::title_egg;
    if (title.width < s.x + s.w || title.height < s.y + s.h) return false;
    const uint32_t first = title.pixels[size_t(s.y) * title.width + s.x];
    for (int y = L::title_egg_active.y; y < s.y + s.h; ++y)
        for (int x = s.x; x < s.x + s.w; ++x)
            if (title.pixels[size_t(y) * title.width + x] != first) return true;
    return false;
}

void draw_titlebar_buttons(Canvas& c, const Image& title, const MainState& st) {
    switch (st.pressed) {
    case Control::options:  c.blit(title, L::btn_options_down, L::pos_options); break;
    case Control::minimize: c.blit(title, L::btn_minimize_down, L::pos_minimize); break;
    case Control::shade:    c.blit(title, st.shade ? L::btn_shade_on_down : L::btn_shade_down, L::pos_shade); break;
    case Control::close:    c.blit(title, L::btn_close_down, L::pos_close); break;
    default: break;
    }
}

void draw_time(Canvas& c, const Skin& skin, const MainState& st) {
    if (!st.show_time) return;
    const Image& ex = skin.bitmap(SkinBitmap::nums_ex);
    const Image& digits = ex.empty() ? skin.bitmap(SkinBitmap::numbers) : ex;

    const int minutes = std::min(st.time_seconds / 60, 999);
    const int seconds = st.time_seconds % 60;
    const int d[4] = { minutes / 10 % 10, minutes % 10, seconds / 10, seconds % 10 };
    for (int i = 0; i < 4; ++i) c.blit(digits, d[i] * L::digit_w, 0, L::digit_w, L::digit_h, L::pos_digits[i].x, L::pos_digits[i].y);

    // The minus slot: the 9px blank cell (90,0) (numbers.bmp has it too when 99px wide), then the
    // hundreds of minutes, then the minus on top, like Winamp (nums_ex's minus hides the hundreds).
    if (!ex.empty() || digits.width >= L::no_minus_nums_ex.x + L::no_minus_nums_ex.w) c.blit(digits, L::no_minus_nums_ex, L::pos_minus);
    if (minutes >= 100) c.blit(digits, minutes / 100 * L::digit_w, 0, L::digit_w, L::digit_h, L::pos_minus.x, L::pos_minus.y);
    if (st.time_negative) {
        if (!ex.empty()) c.blit(ex, L::minus_nums_ex, L::pos_minus);
        else c.blit(digits, L::minus_numbers, L::pos_minus_dash);
    }
}

void draw_marquee(Canvas& c, const Image& font, const MainState& st) {
    if (st.marquee.empty()) return;
    const size_t len = text_length(st.marquee.c_str());

    c.set_clip(L::pos_title_text.x, L::pos_title_text.y, L::title_text_w, font_char_h);
    if (len >= static_cast<size_t>(L::marquee_max_chars)) {
        // The separator between repeats is part of the title format (Options).
        const int loop = static_cast<int>(len) * font_char_w;
        const int offset = (st.marquee_px % loop + loop) % loop;
        const std::string looped = st.marquee + st.marquee;
        draw_text(c, font, L::pos_title_text.x - offset, L::pos_title_text.y, looped.c_str());
    } else {
        draw_text(c, font, L::pos_title_text.x, L::pos_title_text.y, st.marquee.c_str());
    }
    c.reset_clip();
}

} // namespace

// Winamp's area: background over x 111..265, y 24..36; text from y 23, scrolling once wider than 146 px.
void draw_title_font(HDC dc, int scale, HFONT font, const MainState& st) {
    if (!font) return;
    const std::wstring text = pfc::stringcvt::string_wide_from_utf8(st.marquee.c_str()).get_ptr();
    const RECT area = { 111 * scale, 24 * scale, 265 * scale, 36 * scale };
    const int saved = SaveDC(dc);
    IntersectClipRect(dc, area.left, area.top, area.right, area.bottom);
    HBRUSH bg = CreateSolidBrush(st.title_bg);
    FillRect(dc, &area, bg);
    DeleteObject(bg);
    SelectObject(dc, font);
    SetTextColor(dc, st.title_fg);
    SetBkMode(dc, TRANSPARENT);
    auto width_of = [&](const std::wstring& s) {
        RECT r = {};
        DrawTextW(dc, s.c_str(), static_cast<int>(s.size()), &r, DT_CALCRECT | DT_SINGLELINE | DT_NOPREFIX);
        return static_cast<int>(r.right - r.left);
    };
    std::wstring shown = text;
    int offset = 0;
    const int text_w = width_of(text);
    if (text_w > 146 * scale) {
        const int loop = text_w;
        offset = ((st.marquee_px * scale) % loop + loop) % loop;
        shown = text + text;
    }
    RECT r = { 111 * scale - offset, 23 * scale, 265 * scale, area.bottom };
    DrawTextW(dc, shown.c_str(), static_cast<int>(shown.size()), &r, DT_LEFT | DT_SINGLELINE | DT_NOPREFIX);
    RestoreDC(dc, saved);
}

namespace {

// Leading blanks use Winamp's blank cell, not the space glyph. A 3-digit field shows larger values like
// Winamp's kbps: 1000..9999 as hundreds with 'H' (1411 -> "14H"), from 10000 as ten-thousands with 'C'.
void draw_number(Canvas& c, const Image& font, L::Pos pos, int value, int width) {
    if (value < 0) return;
    char buf[16];
    if (width == 3 && value >= 10000) snprintf(buf, sizeof(buf), "%2dC", std::min(value / 10000, 99));
    else if (width == 3 && value >= 1000) snprintf(buf, sizeof(buf), "%02dH", value / 100);
    else snprintf(buf, sizeof(buf), "%*d", width, std::min(value, width == 3 ? 999 : 99));
    for (int i = 0; buf[i]; ++i) {
        const int x = pos.x + i * font_char_w;
        if (buf[i] == ' ') c.blit(font, L::text_blank, { x, pos.y });
        else if (buf[i] >= '0' && buf[i] <= '9') c.blit(font, (buf[i] - '0') * font_char_w, font_char_h, font_char_w, font_char_h, x, pos.y);
        else { const char ch[2] = { buf[i], 0 }; draw_text(c, font, x, pos.y, ch); }
    }
}

void render_shade(Canvas& c, const Skin& skin, const MainState& st) {
    const Image& title = skin.bitmap(SkinBitmap::titlebar);
    const Image& font = skin.bitmap(SkinBitmap::text);
    c.clear(0);
    c.blit(title, st.active ? L::shade_bg_active : L::shade_bg, { 0, 0 });
    draw_titlebar_buttons(c, title, st);

    if (st.show_time) {
        // 3px minus (or blank) and text.bmp digits; the colon belongs to the background. From 100 minutes
        // the hundreds digit takes x=129 and the minus moves 4px left, like Winamp.
        const int m = std::min(st.time_seconds / 60, 999), s = st.time_seconds % 60;
        const int y = L::pos_shade_minus.y;
        c.blit(font, 142, 0, 3, font_char_h, L::pos_shade_minus.x - 4, y);
        if (m >= 100) c.blit(font, m / 100 * font_char_w, font_char_h, font_char_w, font_char_h, L::pos_shade_minus.x - 1, y);
        else c.blit(font, 142, 0, 3, font_char_h, L::pos_shade_minus.x, y);
        if (st.time_negative) c.blit(font, 75, 6, 3, font_char_h, L::pos_shade_minus.x - (m >= 100 ? 4 : 0), y);
        const int d[4] = { m / 10 % 10, m % 10, s / 10, s % 10 };
        for (int i = 0; i < 4; ++i)
            c.blit(font, d[i] * font_char_w, font_char_h, font_char_w, font_char_h, L::shade_time_x[i], L::pos_shade_minus.y);
    }

    if (st.play != PlayState::stopped && st.seekable) {
        c.blit(title, L::shade_posbar_bg, L::pos_shade_posbar);
        // Thumb offset 1..13 from position 0..256 (*12/256 + 1); left/middle/right sprite below 6 / 9 / above.
        const int pos256 = static_cast<int>(std::clamp(st.position, 0.0, 1.0) * 256);
        const int op = std::clamp(pos256 * 12 / 256 + 1, 1, 13);
        const L::Sprite thumb = op < 6 ? L::shade_posbar_thumb_left : op < 9 ? L::shade_posbar_thumb : L::shade_posbar_thumb_right;
        c.blit(title, thumb, { L::pos_shade_posbar.x + op, L::pos_shade_posbar.y });
    }

    if (st.vis) st.vis->render(c, skin, L::pos_shade_visualizer, true);
}

} // namespace

void render_main(Canvas& c, const Skin& skin, const MainState& st) {
    if (st.shade) { render_shade(c, skin, st); return; }

    auto& main = skin.bitmap(SkinBitmap::main);
    auto& title = skin.bitmap(SkinBitmap::titlebar);
    auto& cbuttons = skin.bitmap(SkinBitmap::cbuttons);
    auto& shufrep = skin.bitmap(SkinBitmap::shufrep);
    auto& monoster = skin.bitmap(SkinBitmap::monoster);
    auto& playpaus = skin.bitmap(SkinBitmap::playpaus);
    auto& posbar = skin.bitmap(SkinBitmap::posbar);
    auto& font = skin.bitmap(SkinBitmap::text);
    auto is = [&](Control id) { return st.pressed == id; };

    c.clear(0);
    c.blit(main, 0, 0, c.width(), c.height(), 0, 0);

    if (st.easter_egg && has_easter_egg(title)) c.blit(title, st.active ? L::title_egg_active : L::title_egg, L::pos_title_bar);
    else c.blit(title, st.active ? L::title_bar_active : L::title_bar, L::pos_title_bar);
    draw_titlebar_buttons(c, title, st);

    // Clutter bar: background plus selected states for pressed buttons and active toggles.
    // When not always shown, Winamp draws its blank variant until a button is pressed.
    const bool clutter_pressed = is(Control::clutter_o) || is(Control::clutter_a) || is(Control::clutter_i)
        || is(Control::clutter_d) || is(Control::clutter_v);
    if (!st.clutterbar && !clutter_pressed) {
        c.blit(title, L::clutter_bar_hidden, L::pos_clutter);
    } else {
        c.blit(title, L::clutter_bar, L::pos_clutter);
        if (is(Control::clutter_o)) c.blit(title, L::clutter_o_on, L::pos_clutter_o);
        if (is(Control::clutter_a) || st.always_on_top) c.blit(title, L::clutter_a_on, L::pos_clutter_a);
        if (is(Control::clutter_i)) c.blit(title, L::clutter_i_on, L::pos_clutter_i);
        if (is(Control::clutter_d) || st.double_size) c.blit(title, L::clutter_d_on, L::pos_clutter_d);
        if (is(Control::clutter_v)) c.blit(title, L::clutter_v_on, L::pos_clutter_v);
    }

    switch (st.play) {
    case PlayState::playing:
        c.blit(playpaus, L::ind_playing, L::pos_play_state);
        c.blit(playpaus, L::ind_working, L::pos_work_ind);
        break;
    case PlayState::paused:
        c.blit(playpaus, L::ind_filler, L::pos_work_ind);
        c.blit(playpaus, L::ind_paused, L::pos_play_state);
        break;
    case PlayState::stopped:
        c.blit(playpaus, L::ind_filler, L::pos_work_ind);
        c.blit(playpaus, L::ind_stopped, L::pos_play_state);
        break;
    }

    draw_time(c, skin, st);
    if (st.vis) st.vis->render(c, skin, L::pos_visualizer, false);
    if (st.title_font) {
        draw_title_font(c.dc(), 1, st.title_font, st);
        GdiFlush();
    } else {
        draw_marquee(c, font, st);
    }
    draw_number(c, font, L::pos_kbps, st.kbps, 3);
    draw_number(c, font, L::pos_khz, st.khz, 2);

    const bool playing = st.play != PlayState::stopped;
    c.blit(monoster, playing && st.channels == 1 ? L::mono_on : L::mono, L::pos_mono);
    c.blit(monoster, playing && st.channels >= 2 ? L::stereo_on : L::stereo, L::pos_stereo);

    draw_slider(c, skin.bitmap(SkinBitmap::volume), L::volume_bg, L::volume_thumb, L::volume_thumb_down,
        is(Control::volume), L::pos_volume, st.volume, st.volume, L::volume_travel);
    draw_slider(c, skin.bitmap(SkinBitmap::balance), L::balance_bg, L::balance_thumb, L::balance_thumb_down,
        is(Control::balance), L::pos_balance, std::abs(st.balance),
        (12 + static_cast<int>(st.balance * 127) * 12 / 127) / double(L::balance_travel), L::balance_travel); // centre 12, truncated toward it

    if (st.eq_on) c.blit(shufrep, is(Control::eq) ? L::eq_btn_on_down : L::eq_btn_on, L::pos_eq_btn);
    else c.blit(shufrep, is(Control::eq) ? L::eq_btn_down : L::eq_btn, L::pos_eq_btn);
    if (st.pl_on) c.blit(shufrep, is(Control::pl) ? L::pl_btn_on_down : L::pl_btn_on, L::pos_pl_btn);
    else c.blit(shufrep, is(Control::pl) ? L::pl_btn_down : L::pl_btn, L::pos_pl_btn);

    if (playing) {
        c.blit(posbar, L::posbar_bg, L::pos_posbar);
        if (st.seekable) {
            const int x = L::pos_posbar.x + static_cast<int>(std::clamp(st.position, 0.0, 1.0) * 256) * (L::posbar_bg.w - L::posbar_thumb.w) / 256;
            c.blit(posbar, is(Control::posbar) ? L::posbar_thumb_down : L::posbar_thumb, { x, L::pos_posbar.y });
        }
    }

    c.blit(cbuttons, is(Control::prev) ? down(L::cb_prev, L::cb_down_dy) : L::cb_prev, L::pos_prev);
    c.blit(cbuttons, is(Control::play) ? down(L::cb_play, L::cb_down_dy) : L::cb_play, L::pos_play);
    c.blit(cbuttons, is(Control::pause) ? down(L::cb_pause, L::cb_down_dy) : L::cb_pause, L::pos_pause);
    c.blit(cbuttons, is(Control::stop) ? down(L::cb_stop, L::cb_down_dy) : L::cb_stop, L::pos_stop);
    c.blit(cbuttons, is(Control::next) ? down(L::cb_next, L::cb_down_dy) : L::cb_next, L::pos_next);
    c.blit(cbuttons, is(Control::eject) ? down(L::cb_eject, L::cb_eject_down_dy) : L::cb_eject, L::pos_eject);

    if (st.shuffle) c.blit(shufrep, is(Control::shuffle) ? L::shuffle_btn_on_down : L::shuffle_btn_on, L::pos_shuffle);
    else c.blit(shufrep, is(Control::shuffle) ? L::shuffle_btn_down : L::shuffle_btn, L::pos_shuffle);
    if (st.repeat) c.blit(shufrep, is(Control::repeat) ? L::repeat_btn_on_down : L::repeat_btn_on, L::pos_repeat);
    else c.blit(shufrep, is(Control::repeat) ? L::repeat_btn_down : L::repeat_btn, L::pos_repeat);
}

} // namespace wsz
