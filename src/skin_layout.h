#pragma once

// Classic Winamp 2.x main window geometry: sprite rects in the skin bitmaps and their
// destination positions in the 275x116 window. Coordinates cross-checked against Webamp.
namespace wsz::layout {

struct Sprite { int x, y, w, h; };
struct Pos { int x, y; };

// --- titlebar.bmp ---
constexpr Sprite title_bar          { 27, 15, 275, 14 };
constexpr Sprite title_bar_active   { 27,  0, 275, 14 };
// Winamp's easter egg title bar (type NULLSOFT in Winamp): "It really whips the llama's ass!" in the base skin.
constexpr Sprite title_egg          { 27, 72, 275, 14 };
constexpr Sprite title_egg_active   { 27, 57, 275, 14 };
constexpr Sprite btn_options        {  0,  0, 9, 9 };
constexpr Sprite btn_options_down   {  0,  9, 9, 9 };
constexpr Sprite btn_minimize       {  9,  0, 9, 9 };
constexpr Sprite btn_minimize_down  {  9,  9, 9, 9 };
constexpr Sprite btn_shade          {  0, 18, 9, 9 };
constexpr Sprite btn_shade_down     {  9, 18, 9, 9 };
constexpr Sprite btn_close          { 18,  0, 9, 9 };
constexpr Sprite btn_close_down     { 18,  9, 9, 9 };
constexpr Sprite clutter_bar        {304,  0, 8, 43 };
constexpr Sprite clutter_bar_hidden {312,  0, 8, 43 };
constexpr Pos pos_title_bar { 0, 0 };
constexpr Pos pos_options   { 6, 3 };
constexpr Pos pos_minimize  { 244, 3 };
constexpr Pos pos_shade     { 254, 3 };
constexpr Pos pos_close     { 264, 3 };
constexpr Pos pos_clutter   { 10, 22 };

// --- playpaus.bmp ---
constexpr Sprite ind_playing     {  0, 0, 9, 9 };
constexpr Sprite ind_paused      {  9, 0, 9, 9 };
constexpr Sprite ind_stopped     { 18, 0, 9, 9 };
constexpr Sprite ind_not_working { 36, 0, 3, 9 };
constexpr Sprite ind_working     { 39, 0, 3, 9 };
constexpr Sprite ind_filler      { 27, 0, 2, 9 };  // drawn at pos_work_ind when not playing (matches Winamp)
constexpr Pos pos_play_state  { 26, 28 };
constexpr Pos pos_work_ind    { 24, 28 };

// --- monoster.bmp ---
constexpr Sprite stereo     {  0, 12, 29, 12 };
constexpr Sprite stereo_on  {  0,  0, 29, 12 };
constexpr Sprite mono       { 29, 12, 27, 12 };
constexpr Sprite mono_on    { 29,  0, 27, 12 };
constexpr Pos pos_mono   { 212, 41 };
constexpr Pos pos_stereo { 239, 41 };

// --- volume.bmp / balance.bmp: 28 frames, 15px apart, 13px tall; thumbs at y=422 ---
constexpr int slider_frames = 28;
constexpr int slider_frame_step = 15;
constexpr int slider_frame_h = 13;
constexpr Sprite volume_bg          {  0, 0, 68, 13 };   // y = frame * 15
constexpr Sprite volume_thumb       { 15, 422, 14, 11 };
constexpr Sprite volume_thumb_down  {  0, 422, 14, 11 };
constexpr Sprite balance_bg         {  9, 0, 38, 13 };   // y = frame * 15
constexpr Sprite balance_thumb      { 15, 422, 14, 11 };
constexpr Sprite balance_thumb_down {  0, 422, 14, 11 };
constexpr Pos pos_volume  { 107, 57 };
constexpr Pos pos_balance { 177, 57 };
constexpr int slider_thumb_dy = 1;

// --- shufrep.bmp ---
constexpr Sprite eq_btn               {  0, 61, 23, 12 };
constexpr Sprite eq_btn_down          { 46, 61, 23, 12 };
constexpr Sprite eq_btn_on            {  0, 73, 23, 12 };
constexpr Sprite eq_btn_on_down       { 46, 73, 23, 12 };
constexpr Sprite pl_btn               { 23, 61, 23, 12 };
constexpr Sprite pl_btn_down          { 69, 61, 23, 12 };
constexpr Sprite pl_btn_on            { 23, 73, 23, 12 };
constexpr Sprite pl_btn_on_down       { 69, 73, 23, 12 };
constexpr Sprite shuffle_btn          { 28,  0, 47, 15 };
constexpr Sprite shuffle_btn_down     { 28, 15, 47, 15 };
constexpr Sprite shuffle_btn_on       { 28, 30, 47, 15 };
constexpr Sprite shuffle_btn_on_down  { 28, 45, 47, 15 };
constexpr Sprite repeat_btn           {  0,  0, 28, 15 };
constexpr Sprite repeat_btn_down      {  0, 15, 28, 15 };
constexpr Sprite repeat_btn_on        {  0, 30, 28, 15 };
constexpr Sprite repeat_btn_on_down   {  0, 45, 28, 15 };
constexpr Pos pos_eq_btn  { 219, 58 };
constexpr Pos pos_pl_btn  { 242, 58 };
constexpr Pos pos_shuffle { 164, 89 };
constexpr Pos pos_repeat  { 210, 89 };

// --- posbar.bmp ---
constexpr Sprite posbar_bg         {   0, 0, 248, 10 };
constexpr Sprite posbar_thumb      { 248, 0, 29, 10 };
constexpr Sprite posbar_thumb_down { 278, 0, 29, 10 };
constexpr Pos pos_posbar { 16, 72 };

// --- cbuttons.bmp: normal row y=0, pressed row below ---
constexpr Sprite cb_prev  {   0, 0, 23, 18 };
constexpr Sprite cb_play  {  23, 0, 23, 18 };
constexpr Sprite cb_pause {  46, 0, 23, 18 };
constexpr Sprite cb_stop  {  69, 0, 23, 18 };
constexpr Sprite cb_next  {  92, 0, 22, 18 };
constexpr Sprite cb_eject { 114, 0, 22, 16 };
constexpr int cb_down_dy = 18;
constexpr int cb_eject_down_dy = 16;
constexpr Pos pos_prev  {  16, 88 };
constexpr Pos pos_play  {  39, 88 };
constexpr Pos pos_pause {  62, 88 };
constexpr Pos pos_stop  {  85, 88 };
constexpr Pos pos_next  { 108, 88 };
constexpr Pos pos_eject { 136, 89 };

// --- clutter bar pressed/selected states (titlebar.bmp) ---
constexpr Sprite clutter_o_on { 304, 47, 8, 8 };
constexpr Sprite clutter_a_on { 312, 55, 8, 7 };
constexpr Sprite clutter_i_on { 320, 62, 8, 7 };
constexpr Sprite clutter_d_on { 328, 69, 8, 8 };
constexpr Sprite clutter_v_on { 336, 77, 8, 7 };
constexpr Pos pos_clutter_o { 10, 25 };
constexpr Pos pos_clutter_a { 10, 33 };
constexpr Pos pos_clutter_i { 10, 40 };
constexpr Pos pos_clutter_d { 10, 47 };
constexpr Pos pos_clutter_v { 10, 55 };

// --- windowshade mode (titlebar.bmp), window is 275x14 ---
constexpr int shade_height = 14;
constexpr Sprite shade_bg                { 27, 42, 275, 14 };
constexpr Sprite shade_bg_active         { 27, 29, 275, 14 };
constexpr Sprite btn_shade_on            {  0, 27, 9, 9 };
constexpr Sprite btn_shade_on_down       {  9, 27, 9, 9 };
constexpr Sprite shade_posbar_bg         {  0, 36, 17, 7 };
constexpr Sprite shade_posbar_thumb      { 20, 36, 3, 7 };
constexpr Sprite shade_posbar_thumb_left { 17, 36, 3, 7 };
constexpr Sprite shade_posbar_thumb_right{ 23, 36, 3, 7 };
constexpr Pos pos_shade_posbar { 226, 4 };
constexpr Pos pos_shade_time   { 127, 4 };

// --- time display: numbers.bmp / nums_ex.bmp, 9x13 digits ---
constexpr int digit_w = 9;
constexpr int digit_h = 13;
constexpr Sprite minus_numbers   { 20, 6, 5, 1 };   // numbers.bmp dash
constexpr Sprite minus_nums_ex   { 99, 0, 9, 13 };
constexpr Sprite no_minus_nums_ex{ 90, 0, 9, 13 };
constexpr Pos pos_minus        { 38, 26 };
constexpr Pos pos_minus_dash   { 38, 32 };
constexpr Pos pos_digits[4] = { { 48, 26 }, { 60, 26 }, { 78, 26 }, { 90, 26 } };

// --- text areas (text.bmp font) ---
constexpr Pos pos_title_text { 111, 27 };
constexpr int title_text_w = 154;
constexpr int marquee_max_chars = 31;
constexpr Pos pos_kbps { 111, 43 };
constexpr Pos pos_khz  { 156, 43 };

// --- not drawn yet ---
constexpr Pos pos_visualizer { 24, 43 };   // 76x16
constexpr Pos pos_shade_visualizer { 79, 5 }; // 38x5

} // namespace wsz::layout
