#pragma once

// Thin layer mapping Winamp controls onto foobar2000 playback APIs. Main thread only.
namespace wsz::player {

// Shuffle/repeat buttons reflect fb2k's playback order: any Random/Shuffle order lights shuffle,
// any Repeat order lights repeat. Each button opens a menu of the matching orders.
struct OrderState { bool shuffle = false; bool repeat = false; };

OrderState order_state();
// Orders offered by the shuffle / repeat menu (those fb2k actually has), "Default" excluded.
std::vector<pfc::string8> orders(bool shuffle);
pfc::string8 active_order();
void set_order(const char* name);

// Same curve as the SDK's VolumeMap (Default UI slider).
double db_to_slider(double db);
double slider_to_db(double slider);

// The playing track's title with script, as playback_control::playback_format_title, plus %list_index% and
// %list_total% from its place in the playing playlist (Winamp's "3. Artist - Title"; fb2k defines them only in
// playlist views).
void format_playing_title(pfc::string_base& out, const titleformat_object::ptr& script);

void play();
void pause();
void stop();
void toggle_stop_after_current();
void previous();
void next();
void first_track(bool last); // Winamp's Start/End of list: playing playlist's first/last entry
// Transport button clicks with Winamp's modifiers: Prev/Next Shift = seek -/+5 s, Ctrl = first/last
// track; Stop Ctrl = stop after current.
void prev_button();
void next_button();
void stop_button();
void open_files();
void show_file_info();

} // namespace wsz::player
