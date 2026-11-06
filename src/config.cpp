#include "stdafx.h"
#include "config.h"
#include "guids.h"

namespace wsz::config {
cfg_int window_x(guids::cfg_window_x, CW_USEDEFAULT);
cfg_int window_y(guids::cfg_window_y, CW_USEDEFAULT);
cfg_string skin_path(guids::cfg_skin_path, "");
cfg_string skins_folder(guids::cfg_skins_folder, "");
cfg_bool time_remaining(guids::cfg_time_remaining, false);
cfg_bool double_size(guids::cfg_double_size, false);
cfg_bool shade(guids::cfg_shade, false);
cfg_float balance(guids::cfg_balance, 0.0);
cfg_bool winamp_hotkeys(guids::cfg_winamp_hotkeys, true);
cfg_bool balance_dsp(guids::cfg_balance_dsp, true);
cfg_int balance_snap(guids::cfg_balance_snap, 25);
cfg_bool dim_titlebars(guids::cfg_dim_titlebars, true);
cfg_bool clutterbar(guids::cfg_clutterbar, true);
cfg_bool oldschool_pl_buttons(guids::cfg_oldschool_pl_buttons, false);
cfg_bool tooltips(guids::cfg_tooltips, true);
cfg_bool title_bitmap_font(guids::cfg_title_bitmap_font, true);
cfg_bool title_font_alt(guids::cfg_title_font_alt, false);
cfg_string format_main(guids::cfg_format_main, default_format_main);
cfg_string format_playlist(guids::cfg_format_playlist, default_format_playlist);
cfg_string format_playlist_shade(guids::cfg_format_playlist_shade, default_format_playlist_shade);
cfg_bool lock_ui(guids::cfg_lock_ui, false);
cfg_bool playlist_view_skin_font(guids::cfg_playlist_view_skin_font, true);
cfg_bool skin_cursors(guids::cfg_skin_cursors, true);
cfg_bool snap(guids::cfg_snap, true);
cfg_int snap_distance(guids::cfg_snap_distance, 10);
cfg_bool smooth_resize(guids::cfg_smooth_resize, false);
cfg_bool double_size_all(guids::cfg_double_size_all, true);
cfg_bool eq_double(guids::cfg_eq_double, false);
cfg_bool pl_double(guids::cfg_pl_double, false);
cfg_int vis_mode(guids::cfg_vis_mode, 0);
cfg_int vis_analyzer(guids::cfg_vis_analyzer, 0);
cfg_bool vis_thin_bands(guids::cfg_vis_thin_bands, false);
cfg_bool vis_peaks(guids::cfg_vis_peaks, true);
cfg_int vis_scope(guids::cfg_vis_scope, 1);
cfg_int vis_refresh(guids::cfg_vis_refresh, 2);
cfg_int vis_falloff(guids::cfg_vis_falloff, 2);
cfg_int vis_peak_falloff(guids::cfg_vis_peak_falloff, 1);

VisOptions vis_options() {
    return { static_cast<int>(vis_analyzer.get()), vis_thin_bands.get(), vis_peaks.get(), static_cast<int>(vis_scope.get()),
        static_cast<int>(vis_refresh.get()), static_cast<int>(vis_falloff.get()), static_cast<int>(vis_peak_falloff.get()) };
}

void set_vis_options(const VisOptions& o) {
    vis_analyzer = o.analyzer;
    vis_thin_bands = o.thin_bands;
    vis_peaks = o.peaks;
    vis_scope = o.scope;
    vis_refresh = o.refresh;
    vis_falloff = o.falloff;
    vis_peak_falloff = o.peak_falloff;
}

cfg_bool eq_visible(guids::cfg_eq_visible, true);
cfg_int eq_x(guids::cfg_eq_x, CW_USEDEFAULT);
cfg_int eq_y(guids::cfg_eq_y, CW_USEDEFAULT);
cfg_bool eq_shade(guids::cfg_eq_shade, false);
cfg_bool eq_on(guids::cfg_eq_on, false);
cfg_bool eq_auto(guids::cfg_eq_auto, false);
cfg_string eq_values(guids::cfg_eq_values, "");

cfg_bool pl_visible(guids::cfg_pl_visible, true);
cfg_int pl_x(guids::cfg_pl_x, CW_USEDEFAULT);
cfg_int pl_y(guids::cfg_pl_y, CW_USEDEFAULT);
cfg_int pl_w(guids::cfg_pl_w, 0);
cfg_int pl_h(guids::cfg_pl_h, 4);
cfg_int pl_width(guids::cfg_pl_width, -1);
cfg_int pl_height(guids::cfg_pl_height, -1);
cfg_bool pl_shade(guids::cfg_pl_shade, false);
cfg_bool pl_use_view(guids::cfg_pl_use_view, false);
cfg_blob pl_view_config(guids::cfg_pl_view_config);

cfg_blob element_windows(guids::cfg_element_windows);
}
