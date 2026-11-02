#include "stdafx.h"
#include "player.h"
#include "config.h"

#include <cmath>

namespace wsz::player {
namespace {

constexpr char order_default[] = "Default";
constexpr char order_repeat[] = "Repeat (playlist)";
constexpr char order_shuffle[] = "Shuffle (tracks)";

bool starts_with(const char* s, const char* prefix) { return strncmp(s, prefix, strlen(prefix)) == 0; }

bool is_shuffle_order(const char* name) { return starts_with(name, "Shuffle") || starts_with(name, "Random"); }
bool is_repeat_order(const char* name) { return starts_with(name, "Repeat"); }

pfc::string8 active_order_name() {
    auto pm = playlist_manager::get();
    return pm->playback_order_get_name(pm->playback_order_get_active());
}

void set_order(const char* name) {
    auto pm = playlist_manager::get();
    const size_t n = pm->playback_order_get_count();
    for (size_t i = 0; i < n; ++i) {
        if (strcmp(pm->playback_order_get_name(i), name) == 0) {
            pm->playback_order_set_active(i);
            return;
        }
    }
}

} // namespace

OrderState order_state() {
    const pfc::string8 name = active_order_name();
    OrderState st;
    st.shuffle = is_shuffle_order(name);
    st.repeat = is_repeat_order(name) || (st.shuffle && config::repeat_with_shuffle.get());
    return st;
}

void toggle_shuffle() {
    const OrderState st = order_state();
    if (st.shuffle) {
        set_order(st.repeat ? order_repeat : order_default);
    } else {
        config::repeat_with_shuffle = st.repeat;
        set_order(order_shuffle);
    }
}

void toggle_repeat() {
    const OrderState st = order_state();
    if (st.shuffle) {
        // fb2k has no "shuffle without repeat"; keep the button state ourselves.
        config::repeat_with_shuffle = !config::repeat_with_shuffle.get();
    } else {
        set_order(st.repeat ? order_default : order_repeat);
    }
}

double db_to_slider(double db) { return std::clamp(std::pow(2.0, db / 10.0), 0.0, 1.0); }

double slider_to_db(double slider) {
    if (slider <= 0) return -100.0;
    return std::max(-100.0, 10.0 * std::log2(slider));
}

void play() {
    auto pc = playback_control::get();
    if (pc->is_paused()) pc->pause(false);
    else pc->start(playback_control::track_command_play);
}

void pause() {
    auto pc = playback_control::get();
    if (pc->is_playing()) pc->pause(!pc->is_paused());
}

void stop() { playback_control::get()->stop(); }
void previous() { standard_commands::main_previous(); }
void next() { standard_commands::main_next(); }
void open_files() { standard_commands::main_open(); }

void show_file_info() {
    metadb_handle_ptr track;
    if (!playback_control::get()->get_now_playing(track) && !playlist_manager::get()->activeplaylist_get_focus_item_handle(track)) return;
    metadb_handle_list items;
    items.add_item(track);
    standard_commands::run_context(standard_commands::guid_context_file_properties, items);
}

} // namespace wsz::player
