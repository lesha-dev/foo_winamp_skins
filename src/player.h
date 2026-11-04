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

void play();
void pause();
void stop();
void toggle_stop_after_current();
void previous();
void next();
void open_files();
void show_file_info();

} // namespace wsz::player
