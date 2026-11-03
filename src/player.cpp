#include "stdafx.h"
#include "player.h"
#include "config.h"

#include <cmath>

namespace wsz::player {
namespace {

bool starts_with(const char* s, const char* prefix) { return strncmp(s, prefix, strlen(prefix)) == 0; }

bool is_shuffle_order(const char* name) { return starts_with(name, "Shuffle") || starts_with(name, "Random"); }
bool is_repeat_order(const char* name) { return starts_with(name, "Repeat"); }

} // namespace

pfc::string8 active_order() {
    auto pm = playlist_manager::get();
    return pm->playback_order_get_name(pm->playback_order_get_active());
}

std::vector<pfc::string8> orders(bool shuffle) {
    std::vector<pfc::string8> out;
    auto pm = playlist_manager::get();
    const size_t n = pm->playback_order_get_count();
    for (size_t i = 0; i < n; ++i) {
        const char* name = pm->playback_order_get_name(i);
        if (shuffle ? is_shuffle_order(name) : is_repeat_order(name)) out.emplace_back(name);
    }
    return out;
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

OrderState order_state() {
    const pfc::string8 name = active_order();
    OrderState st;
    st.shuffle = is_shuffle_order(name);
    st.repeat = is_repeat_order(name);
    return st;
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
