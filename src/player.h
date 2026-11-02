#pragma once

// Thin layer mapping Winamp controls onto foobar2000 playback APIs. Main thread only.
namespace wsz::player {

struct OrderState { bool shuffle = false; bool repeat = false; };

OrderState order_state();
void toggle_shuffle();
void toggle_repeat();

// Same curve as the SDK's VolumeMap (Default UI slider).
double db_to_slider(double db);
double slider_to_db(double slider);

void play();
void pause();
void stop();
void previous();
void next();
void open_files();
void show_file_info();

} // namespace wsz::player
