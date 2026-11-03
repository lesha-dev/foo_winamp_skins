#include "stdafx.h"
#include "config.h"
#include "guids.h"

namespace wsz::config {
cfg_int window_x(guids::cfg_window_x, CW_USEDEFAULT);
cfg_int window_y(guids::cfg_window_y, CW_USEDEFAULT);
cfg_string skin_path(guids::cfg_skin_path, "");
cfg_bool time_remaining(guids::cfg_time_remaining, false);
cfg_bool double_size(guids::cfg_double_size, false);
cfg_bool always_on_top(guids::cfg_always_on_top, false);
cfg_bool shade(guids::cfg_shade, false);
cfg_bool repeat_with_shuffle(guids::cfg_repeat_with_shuffle, false);
cfg_float balance(guids::cfg_balance, 0.0);
cfg_bool winamp_hotkeys(guids::cfg_winamp_hotkeys, true);
cfg_bool balance_dsp(guids::cfg_balance_dsp, true);
cfg_int balance_snap(guids::cfg_balance_snap, 25);
cfg_int vis_mode(guids::cfg_vis_mode, 0);

cfg_bool eq_visible(guids::cfg_eq_visible, false);
cfg_int eq_x(guids::cfg_eq_x, CW_USEDEFAULT);
cfg_int eq_y(guids::cfg_eq_y, CW_USEDEFAULT);
cfg_bool eq_shade(guids::cfg_eq_shade, false);
cfg_bool eq_on(guids::cfg_eq_on, false);
cfg_bool eq_auto(guids::cfg_eq_auto, false);
cfg_string eq_values(guids::cfg_eq_values, "");

cfg_bool pl_visible(guids::cfg_pl_visible, false);
cfg_int pl_x(guids::cfg_pl_x, CW_USEDEFAULT);
cfg_int pl_y(guids::cfg_pl_y, CW_USEDEFAULT);
cfg_int pl_w(guids::cfg_pl_w, 0);
cfg_int pl_h(guids::cfg_pl_h, 4);
cfg_bool pl_shade(guids::cfg_pl_shade, false);
}
