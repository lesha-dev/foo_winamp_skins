#pragma once

#include "vis_options.h"

namespace wsz::config {
extern cfg_int window_x;
extern cfg_int window_y;
extern cfg_string skin_path;
extern cfg_string skins_folder;        // custom skins folder; empty = <profile>\winamp-skins
extern cfg_bool time_remaining;
extern cfg_bool autoscroll;           // main window song title scrolls by itself
extern cfg_bool double_size;
extern cfg_bool shade;
extern cfg_float balance;            // use wsz::balance, which caches it for the audio thread
extern cfg_bool winamp_hotkeys;       // Z X C V B L, arrows, Ctrl+D/A/W in the main window
extern cfg_bool balance_dsp;          // use wsz::balance::set_enabled()
extern cfg_bool dim_titlebars;        // inactive windows show the dimmed titlebar
extern cfg_bool clutterbar;           // always show the clutterbar (else only while pressed)
extern cfg_bool oldschool_pl_buttons; // playlist menus: hold, pick on release (else click to open, click to pick)
extern cfg_bool tooltips;             // tooltips on the skinned windows
extern cfg_bool title_bitmap_font;    // main window title in the skin's text.bmp font (else playlist font)
extern cfg_bool title_font_alt;       // double size: draw the font title at full resolution
// Title formatting scripts (foobar2000 syntax). $num(%list_index%,0) is Winamp's unpadded position.
constexpr const char default_format_main[] = "[$num(%list_index%,0). ][%artist% - ]%title%[ '('%length%')']'  ***  '";
constexpr const char default_format_playlist[] = "$num(%list_index%,0). [%artist% - ]%title%";
constexpr const char default_format_playlist_shade[] = "[%artist% - ]%title%";
extern cfg_string format_main;           // main window song title
extern cfg_string format_playlist;       // playlist editor rows (track length stays right-aligned)
extern cfg_string format_playlist_shade; // playlist editor in windowshade mode
extern cfg_bool lock_ui;              // dragging any skinned window moves everything docked to it
extern cfg_bool playlist_view_skin_font; // hosted Playlist View uses the skin's playlist font
extern cfg_bool skin_cursors;         // mouse cursors from the skin's *.cur files
extern cfg_bool snap;                 // dock windows to each other / screen edges (Shift inverts while dragging)
extern cfg_int snap_distance;         // px
extern cfg_bool smooth_resize;        // playlist resizes per pixel instead of 25x29 steps
extern cfg_bool title_easter_egg;      // main window: titlebar.bmp's easter egg title bar, when the skin has one
extern cfg_bool double_size_all;      // Double size toggles all windows at once (else only the one it was used on)
extern cfg_bool eq_double;             // equalizer is double size
extern cfg_bool pl_double;             // playlist editor is double size
extern cfg_int balance_snap;          // percent around centre that snaps to 0
extern cfg_int vis_mode;
// Winamp classic vis options (Winamp defaults): analyzer style 0 normal / 1 fire / 2 line; scope style
// 0 dots / 1 lines / 2 solid; refresh = draw every Nth cycle (1..4); falloffs 0 slowest .. 4 fastest.
extern cfg_int vis_analyzer;
extern cfg_bool vis_thin_bands;
extern cfg_bool vis_peaks;
extern cfg_int vis_scope;
extern cfg_int vis_refresh;
extern cfg_int vis_falloff;
extern cfg_int vis_peak_falloff;
VisOptions vis_options();
void set_vis_options(const VisOptions& o);

extern cfg_bool eq_visible;
extern cfg_int eq_x, eq_y;
extern cfg_bool eq_shade;
extern cfg_bool eq_on;
extern cfg_bool eq_auto;
extern cfg_string eq_values;          // "preamp,b1..b10" slider values 0..100 (50 = 0 dB); use wsz::eq

extern cfg_bool pl_visible;
extern cfg_int pl_x, pl_y;
extern cfg_int pl_w, pl_h;            // legacy: size steps beyond the 275x116 minimum (25 / 29 px each)
extern cfg_int pl_width, pl_height;   // px beyond the 275x116 minimum; -1 = take the legacy steps
extern cfg_bool pl_shade;             // VisMode
extern cfg_bool pl_use_view;          // list area shows Default UI's Playlist View
extern cfg_blob pl_view_config;       // its configuration

extern cfg_blob element_windows;      // UI element windows: placement + element configuration (see MainWindow)
}
