#include "stdafx.h"
#include "main_renderer.h"
#include "text_font.h"

namespace wsz {
namespace L = layout;

namespace {

int frame_for(double t) { // t in 0..1
    return std::clamp(static_cast<int>(t * (L::slider_frames - 1) + 0.5), 0, L::slider_frames - 1);
}

L::Sprite down(L::Sprite s, int dy) { return { s.x, s.y + dy, s.w, s.h }; }

void draw_slider(Canvas& c, const Image& img, L::Sprite bg, L::Sprite thumb, L::Sprite thumb_down, bool pressed,
                 L::Pos pos, double frame_t, double thumb_t) {
    const int f = frame_for(frame_t);
    c.blit(img, bg.x, f * L::slider_frame_step, bg.w, L::slider_frame_h, pos.x, pos.y);
    // Skins without a thumb row (bitmap shorter than 433px) just show the background.
    if (img.height >= thumb.y + thumb.h) {
        const int x = pos.x + static_cast<int>(std::clamp(thumb_t, 0.0, 1.0) * (bg.w - thumb.w) + 0.5);
        c.blit(img, pressed ? thumb_down : thumb, { x, pos.y + L::slider_thumb_dy });
    }
}

void draw_titlebar_buttons(Canvas& c, const Image& title, const MainState& st) {
    auto is = [&](Control id) { return st.pressed == id; };
    c.blit(title, is(Control::options) ? L::btn_options_down : L::btn_options, L::pos_options);
    c.blit(title, is(Control::minimize) ? L::btn_minimize_down : L::btn_minimize, L::pos_minimize);
    if (st.shade) c.blit(title, is(Control::shade) ? L::btn_shade_on_down : L::btn_shade_on, L::pos_shade);
    else c.blit(title, is(Control::shade) ? L::btn_shade_down : L::btn_shade, L::pos_shade);
    c.blit(title, is(Control::close) ? L::btn_close_down : L::btn_close, L::pos_close);
}

void draw_time(Canvas& c, const Skin& skin, const MainState& st) {
    if (!st.show_time) return;
    const Image& ex = skin.bitmap(SkinBitmap::nums_ex);
    const Image& digits = ex.empty() ? skin.bitmap(SkinBitmap::numbers) : ex;

    const int minutes = std::min(st.time_seconds / 60, 99);
    const int seconds = st.time_seconds % 60;
    const int d[4] = { minutes / 10, minutes % 10, seconds / 10, seconds % 10 };
    for (int i = 0; i < 4; ++i) c.blit(digits, d[i] * L::digit_w, 0, L::digit_w, L::digit_h, L::pos_digits[i].x, L::pos_digits[i].y);

    if (!ex.empty()) c.blit(ex, st.time_negative ? L::minus_nums_ex : L::no_minus_nums_ex, L::pos_minus);
    else if (st.time_negative) c.blit(digits, L::minus_numbers, L::pos_minus_dash);
}

void draw_marquee(Canvas& c, const Image& font, const MainState& st) {
    if (st.marquee.empty()) return;
    static const char separator[] = "  ***  ";
    const size_t len = text_length(st.marquee.c_str());

    c.set_clip(L::pos_title_text.x, L::pos_title_text.y, L::title_text_w, font_char_h);
    if (len >= static_cast<size_t>(L::marquee_max_chars)) {
        const size_t loop = len + (std::size(separator) - 1);
        const int offset = static_cast<int>(st.marquee_step % loop) * font_char_w;
        const std::string looped = st.marquee + separator + st.marquee;
        draw_text(c, font, L::pos_title_text.x - offset, L::pos_title_text.y, looped.c_str());
    } else {
        draw_text(c, font, L::pos_title_text.x, L::pos_title_text.y, st.marquee.c_str());
    }
    c.reset_clip();
}

void draw_number(Canvas& c, const Image& font, L::Pos pos, int value, int width) {
    if (value < 0) return;
    char buf[16];
    snprintf(buf, sizeof(buf), "%*d", width, std::min(value, width == 3 ? 999 : 99));
    draw_text(c, font, pos.x, pos.y, buf);
}

void render_shade(Canvas& c, const Skin& skin, const MainState& st) {
    const Image& title = skin.bitmap(SkinBitmap::titlebar);
    const Image& font = skin.bitmap(SkinBitmap::text);
    c.clear(0);
    c.blit(title, st.active ? L::shade_bg_active : L::shade_bg, { 0, 0 });
    draw_titlebar_buttons(c, title, st);

    if (st.show_time) {
        char buf[8];
        snprintf(buf, sizeof(buf), "%c%02d:%02d", st.time_negative ? '-' : ' ', std::min(st.time_seconds / 60, 99), st.time_seconds % 60);
        draw_text(c, font, L::pos_shade_time.x - font_char_w, L::pos_shade_time.y, buf);
    }

    if (st.play != PlayState::stopped && st.seekable) {
        c.blit(title, L::shade_posbar_bg, L::pos_shade_posbar);
        const double p = std::clamp(st.position, 0.0, 1.0);
        const L::Sprite thumb = p < 0.25 ? L::shade_posbar_thumb_left : p > 0.75 ? L::shade_posbar_thumb_right : L::shade_posbar_thumb;
        const int x = L::pos_shade_posbar.x + static_cast<int>(p * (L::shade_posbar_bg.w - thumb.w) + 0.5);
        c.blit(title, thumb, { x, L::pos_shade_posbar.y });
    }
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

    c.blit(title, st.active ? L::title_bar_active : L::title_bar, L::pos_title_bar);
    draw_titlebar_buttons(c, title, st);

    // Clutter bar: background plus selected states for pressed buttons and active toggles.
    c.blit(title, L::clutter_bar, L::pos_clutter);
    if (is(Control::clutter_o)) c.blit(title, L::clutter_o_on, L::pos_clutter_o);
    if (is(Control::clutter_a) || st.always_on_top) c.blit(title, L::clutter_a_on, L::pos_clutter_a);
    if (is(Control::clutter_i)) c.blit(title, L::clutter_i_on, L::pos_clutter_i);
    if (is(Control::clutter_d) || st.double_size) c.blit(title, L::clutter_d_on, L::pos_clutter_d);
    if (is(Control::clutter_v)) c.blit(title, L::clutter_v_on, L::pos_clutter_v);

    switch (st.play) {
    case PlayState::playing:
        c.blit(playpaus, L::ind_playing, L::pos_play_state);
        c.blit(playpaus, L::ind_not_working, L::pos_work_ind);
        break;
    case PlayState::paused:
        c.blit(playpaus, L::ind_paused, L::pos_play_state);
        break;
    case PlayState::stopped:
        c.blit(playpaus, L::ind_stopped, L::pos_play_state);
        break;
    }

    draw_time(c, skin, st);
    draw_marquee(c, font, st);
    draw_number(c, font, L::pos_kbps, st.kbps, 3);
    draw_number(c, font, L::pos_khz, st.khz, 2);

    const bool playing = st.play != PlayState::stopped;
    c.blit(monoster, playing && st.channels == 1 ? L::mono_on : L::mono, L::pos_mono);
    c.blit(monoster, playing && st.channels >= 2 ? L::stereo_on : L::stereo, L::pos_stereo);

    draw_slider(c, skin.bitmap(SkinBitmap::volume), L::volume_bg, L::volume_thumb, L::volume_thumb_down,
        is(Control::volume), L::pos_volume, st.volume, st.volume);
    draw_slider(c, skin.bitmap(SkinBitmap::balance), L::balance_bg, L::balance_thumb, L::balance_thumb_down,
        is(Control::balance), L::pos_balance, std::abs(st.balance), (st.balance + 1.0) / 2.0);

    if (st.eq_on) c.blit(shufrep, is(Control::eq) ? L::eq_btn_on_down : L::eq_btn_on, L::pos_eq_btn);
    else c.blit(shufrep, is(Control::eq) ? L::eq_btn_down : L::eq_btn, L::pos_eq_btn);
    if (st.pl_on) c.blit(shufrep, is(Control::pl) ? L::pl_btn_on_down : L::pl_btn_on, L::pos_pl_btn);
    else c.blit(shufrep, is(Control::pl) ? L::pl_btn_down : L::pl_btn, L::pos_pl_btn);

    if (playing) {
        c.blit(posbar, L::posbar_bg, L::pos_posbar);
        if (st.seekable) {
            const int x = L::pos_posbar.x + static_cast<int>(std::clamp(st.position, 0.0, 1.0) * (L::posbar_bg.w - L::posbar_thumb.w) + 0.5);
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
