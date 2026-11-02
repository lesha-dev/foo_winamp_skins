#include "stdafx.h"
#include "main_renderer.h"

namespace wsz {
namespace L = layout;

namespace {

int frame_for(double t) { // t in 0..1
    return std::clamp(static_cast<int>(t * (L::slider_frames - 1) + 0.5), 0, L::slider_frames - 1);
}

void draw_slider(Canvas& c, const Image& img, L::Sprite bg, L::Sprite thumb, L::Pos pos, double frame_t, double thumb_t) {
    const int f = frame_for(frame_t);
    c.blit(img, bg.x, f * L::slider_frame_step, bg.w, L::slider_frame_h, pos.x, pos.y);
    // Skins without a thumb row (bitmap shorter than 433px) just show the background.
    if (img.height >= thumb.y + thumb.h) {
        const int x = pos.x + static_cast<int>(std::clamp(thumb_t, 0.0, 1.0) * (bg.w - thumb.w) + 0.5);
        c.blit(img, thumb, { x, pos.y + L::slider_thumb_dy });
    }
}

} // namespace

void render_main(Canvas& c, const Skin& skin, const MainState& st) {
    auto& main = skin.bitmap(SkinBitmap::main);
    auto& title = skin.bitmap(SkinBitmap::titlebar);
    auto& cbuttons = skin.bitmap(SkinBitmap::cbuttons);
    auto& shufrep = skin.bitmap(SkinBitmap::shufrep);
    auto& monoster = skin.bitmap(SkinBitmap::monoster);
    auto& playpaus = skin.bitmap(SkinBitmap::playpaus);
    auto& posbar = skin.bitmap(SkinBitmap::posbar);

    c.clear(0);
    c.blit(main, 0, 0, c.width(), c.height(), 0, 0);

    // Titlebar; its bitmap already contains the button faces, but draw them like Winamp does
    // so skins whose titlebar row differs still get their button sprites.
    c.blit(title, st.active ? L::title_bar_active : L::title_bar, L::pos_title_bar);
    c.blit(title, L::btn_options, L::pos_options);
    c.blit(title, L::btn_minimize, L::pos_minimize);
    c.blit(title, L::btn_shade, L::pos_shade);
    c.blit(title, L::btn_close, L::pos_close);
    c.blit(title, L::clutter_bar, L::pos_clutter);

    // Play state indicator.
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

    // Mono / stereo.
    const bool playing = st.play != PlayState::stopped;
    c.blit(monoster, playing && st.channels == 1 ? L::mono_on : L::mono, L::pos_mono);
    c.blit(monoster, playing && st.channels >= 2 ? L::stereo_on : L::stereo, L::pos_stereo);

    // Volume and balance.
    draw_slider(c, skin.bitmap(SkinBitmap::volume), L::volume_bg, L::volume_thumb, L::pos_volume, st.volume, st.volume);
    draw_slider(c, skin.bitmap(SkinBitmap::balance), L::balance_bg, L::balance_thumb, L::pos_balance,
        std::abs(st.balance), (st.balance + 1.0) / 2.0);

    // EQ / PL toggles.
    c.blit(shufrep, st.eq_on ? L::eq_btn_on : L::eq_btn, L::pos_eq_btn);
    c.blit(shufrep, st.pl_on ? L::pl_btn_on : L::pl_btn, L::pos_pl_btn);

    // Position bar: shown while a track is loaded; thumb only when seekable.
    if (playing) {
        c.blit(posbar, L::posbar_bg, L::pos_posbar);
        if (st.seekable) {
            const int x = L::pos_posbar.x + static_cast<int>(std::clamp(st.position, 0.0, 1.0) * (L::posbar_bg.w - L::posbar_thumb.w) + 0.5);
            c.blit(posbar, L::posbar_thumb, { x, L::pos_posbar.y });
        }
    }

    // Transport buttons.
    c.blit(cbuttons, L::cb_prev, L::pos_prev);
    c.blit(cbuttons, L::cb_play, L::pos_play);
    c.blit(cbuttons, L::cb_pause, L::pos_pause);
    c.blit(cbuttons, L::cb_stop, L::pos_stop);
    c.blit(cbuttons, L::cb_next, L::pos_next);
    c.blit(cbuttons, L::cb_eject, L::pos_eject);

    // Shuffle / repeat.
    c.blit(shufrep, st.shuffle ? L::shuffle_btn_on : L::shuffle_btn, L::pos_shuffle);
    c.blit(shufrep, st.repeat ? L::repeat_btn_on : L::repeat_btn, L::pos_repeat);
}

} // namespace wsz
