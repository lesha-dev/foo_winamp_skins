#pragma once

namespace wsz::config {
extern cfg_int window_x;
extern cfg_int window_y;
extern cfg_string skin_path;
extern cfg_bool time_remaining;
extern cfg_bool double_size;
extern cfg_bool always_on_top;
extern cfg_bool shade;
extern cfg_bool repeat_with_shuffle; // repeat button state while a shuffle order is active
extern cfg_float balance;            // use wsz::balance, which caches it for the audio thread
extern cfg_bool winamp_hotkeys;       // Z X C V B L, arrows, Ctrl+D/A/W in the main window
extern cfg_bool balance_dsp;          // use wsz::balance::set_enabled()
extern cfg_int balance_snap;          // percent around centre that snaps to 0
extern cfg_int vis_mode;             // VisMode
}
