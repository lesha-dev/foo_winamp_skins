// Test helper component (not shipped; installed only in skintest's foobar2000 profiles).
//
//   foobar2000.exe ["/skintest_pos:<ms>"] "/skintest_tracks:<folder>"
//
// Puts foobar2000 into the "tracks" state that winamp_capture --tracks gives Winamp, for comparing skins with
// tracks loaded: the active playlist cleared and filled with the first four .mp3 files of <folder> (by name);
// the third one played briefly as winamp_capture does (its VU meter has a level: skins that animate from it, e.g. 3Sat's
// speaker, show the same frame), then paused and moved to 0:05 (or <ms>: the plan's "#tracks_pos"); the first selected; the second focused but not selected; the fourth
// neither. When done it sets the event "Local\skintest_tracks_<pid>" (fb2k_capture waits for it).

#include <foobar2000/SDK/foobar2000.h>

#include <algorithm>
#include <atomic>
#include <string>
#include <thread>
#include <vector>

DECLARE_COMPONENT_VERSION("skintest helper", "1.0", "Test helper for foo_winamp_skins' skin tests (not for normal use).");
VALIDATE_COMPONENT_FILENAME("foo_skintest.dll");

namespace {

enum class Step { idle, starting, playing, seeking, done };
constexpr int kPlayedMs = 300; // how much of the track plays before the pause (see on_playback_new_track)
std::atomic<Step> g_step{Step::idle};
int g_seek_ms = 5000; // "/skintest_pos:<ms>"
t_size g_playlist = ~t_size(0);

// Progress, for fb2k_capture to show when the state doesn't come up: <profile>\skintest.log.
void step_log(const char* msg) {
    pfc::string8 path = core_api::get_profile_path(); // file://...
    if (path.startsWith("file://")) path = path.subString(7);
    path << "\\skintest.log";
    FILE* f = nullptr;
    if (_wfopen_s(&f, pfc::stringcvt::string_wide_from_utf8(path).get_ptr(), L"a") == 0 && f) {
        fprintf(f, "%s\n", msg);
        fclose(f);
    }
}

void signal_done() {
    const std::wstring name = L"Local\\skintest_tracks_" + std::to_wstring(GetCurrentProcessId());
    if (HANDLE ev = CreateEventW(nullptr, TRUE, FALSE, name.c_str())) SetEvent(ev); // kept open: found set later
}

void finish() {
    g_step = Step::done;
    step_log("done");
    auto pm = playlist_manager::get();
    pm->playlist_set_selection(g_playlist, bit_array_true(), bit_array_one(0));
    pm->playlist_set_focus_item(g_playlist, 1);
    signal_done();
}

// A paused seek reports no seek callback: the position is checked (in the main thread) every 50 ms until it's
// there, 3 s at most.
void poll_position() {
    std::thread([] {
        for (int i = 0; i < 60; ++i) {
            Sleep(50);
            const bool last = i == 59;
            fb2k::inMainThread([last] {
                if (g_step != Step::seeking) return;
                const double pos = playback_control::get()->playback_get_position();
                if (pos < g_seek_ms / 1000.0 - 0.05 && !last) return;
                char msg[64];
                snprintf(msg, sizeof msg, "position %.2f", pos);
                step_log(msg);
                finish();
            });
        }
    }).detach();
}

class tracks_callback : public play_callback_static {
public:
    unsigned get_flags() override { return flag_on_playback_new_track | flag_on_playback_seek; }
    void on_playback_new_track(metadb_handle_ptr) override {
        step_log("new track");
        if (g_step != Step::starting) return;
        g_step = Step::playing;
        // winamp_capture pauses when Winamp has the stream's info plus 250 ms: about 0.3 s played. Here: until the
        // position is past kPlayedMs (2 s at most), then paused and moved; the VU keeps the level it had at the pause
        std::thread([] {
            for (int i = 0; i < 100; ++i) {
                Sleep(20);
                fb2k::inMainThread([last = i == 99] {
                    if (g_step != Step::playing) return;
                    if (playback_control::get()->playback_get_position() * 1000 < kPlayedMs && !last) return;
                    char msg[64];
                    snprintf(msg, sizeof msg, "pause at %.3f", playback_control::get()->playback_get_position());
                    step_log(msg);
                    playback_control::get()->pause(true);
                    g_step = Step::seeking;
                    playback_control::get()->playback_seek(g_seek_ms / 1000.0);
                    step_log("seek requested");
                    poll_position();
                });
                if (g_step != Step::playing) return;
            }
        }).detach();
    }
    void on_playback_seek(double) override {}
    void on_playback_starting(play_control::t_track_command, bool) override {}
    void on_playback_stop(play_control::t_stop_reason) override {}
    void on_playback_pause(bool) override {}
    void on_playback_edited(metadb_handle_ptr) override {}
    void on_playback_dynamic_info(const file_info&) override {}
    void on_playback_dynamic_info_track(const file_info&) override {}
    void on_playback_time(double) override {}
    void on_volume_change(float) override {}
};
FB2K_SERVICE_FACTORY(tracks_callback);

class load_done : public completion_notify {
public:
    void on_completion(unsigned) override {
        step_log("info loaded");
        auto pm = playlist_manager::get();
        pm->set_active_playlist(g_playlist);
        pm->set_playing_playlist(g_playlist);
        pm->playlist_set_selection(g_playlist, bit_array_true(), bit_array_false());
        pm->playlist_set_focus_item(g_playlist, 2);
        g_step = Step::starting;
        playback_control::get()->start(playback_control::track_command_play, false); // the focused track: played briefly
    }
};

void setup(const char* folder) {
    std::vector<std::string> files;
    WIN32_FIND_DATAW fd;
    const std::wstring dir = pfc::stringcvt::string_wide_from_utf8(folder).get_ptr();
    HANDLE h = FindFirstFileW((dir + L"\\*.mp3").c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do files.push_back(pfc::stringcvt::string_utf8_from_wide((dir + L"\\" + fd.cFileName).c_str()).get_ptr());
        while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    std::sort(files.begin(), files.end());
    if (files.size() > 4) files.resize(4);
    if (files.size() < 4) {
        FB2K_console_formatter() << "skintest: fewer than 4 .mp3 files in " << folder;
        return;
    }
    auto pm = playlist_manager::get();
    g_playlist = pm->get_active_playlist();
    if (g_playlist == ~t_size(0)) g_playlist = pm->create_playlist("skintest", pfc_infinite, pfc_infinite);
    pm->playlist_clear(g_playlist);
    metadb_handle_list items;
    for (const auto& f : files) {
        pfc::string8 canonical;
        filesystem::g_get_canonical_path(f.c_str(), canonical);
        items.add_item(metadb::get()->handle_create(canonical, 0));
    }
    pm->playlist_add_items(g_playlist, items, bit_array_false());
    step_log("added");
    metadb_io_v2::get()->load_info_async(items, metadb_io::load_info_default, core_api::get_main_window(),
                                         metadb_io_v2::op_flag_no_errors | metadb_io_v2::op_flag_silent | metadb_io_v2::op_flag_delay_ui,
                                         fb2k::service_new<load_done>());
}

class tracks_commandline : public commandline_handler {
public:
    result on_token(const char* token) override {
        static const char pos[] = "/skintest_pos:";
        if (_strnicmp(token, pos, sizeof(pos) - 1) == 0) {
            g_seek_ms = atoi(token + sizeof(pos) - 1);
            return RESULT_PROCESSED;
        }
        static const char prefix[] = "/skintest_tracks:";
        if (_strnicmp(token, prefix, sizeof(prefix) - 1) != 0) return RESULT_NOT_OURS;
        const pfc::string8 folder = token + sizeof(prefix) - 1;
        step_log("token");
        fb2k::inMainThread([folder] { setup(folder.c_str()); }); // after the UI and the skin are up
        return RESULT_PROCESSED;
    }
};
FB2K_SERVICE_FACTORY(tracks_commandline);

} // namespace
