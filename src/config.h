#pragma once

#include "vis_options.h"

namespace wsz::config {
extern cfg_int window_x;
extern cfg_int window_y;
extern cfg_string skin_path;
extern cfg_bool time_remaining;
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
extern cfg_bool skin_cursors;         // mouse cursors from the skin's *.cur files
extern cfg_bool snap;                 // dock windows to each other / screen edges (Shift inverts while dragging)
extern cfg_int snap_distance;         // px
extern cfg_bool smooth_resize;        // playlist resizes per pixel instead of 25x29 steps
extern cfg_bool eq_double_size;       // double size applies to the equalizer too
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

extern cfg_bool art_visible;          // Album Art window
extern cfg_int art_x, art_y;
extern cfg_int art_width, art_height; // px
extern cfg_blob art_element;          // hosted Album Art element's configuration
}
