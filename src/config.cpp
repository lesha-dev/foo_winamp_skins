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
}
