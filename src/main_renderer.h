#pragma once

#include "canvas.h"
#include "controls.h"
#include "skin.h"

#include <string>

namespace wsz {

enum class PlayState { stopped, playing, paused };

// Everything the main window needs to draw one frame.
struct MainState {
    bool active = true;          // window focused -> highlighted titlebar
    bool shade = false;          // windowshade mode (275x14)
    bool always_on_top = false;
    bool double_size = false;
    Control pressed = Control::none; // control drawn in its pressed state

    PlayState play = PlayState::stopped;
    int channels = 0;            // 0 = unknown/stopped
    double volume = 1.0;         // 0..1 slider position
    double balance = 0.0;        // -1..1
    bool seekable = false;
    double position = 0.0;       // 0..1
    bool shuffle = false;
    bool repeat = false;
    bool eq_on = false;
    bool pl_on = false;

    bool show_time = false;      // false while stopped, and on the "off" phase of the pause blink
    int time_seconds = 0;        // absolute value to display
    bool time_negative = false;  // remaining-time mode

    std::string marquee;         // title or slider feedback text
    unsigned marquee_step = 0;   // scroll position in characters (only used for long text)
    int kbps = -1;               // -1 = blank
    int khz = -1;
};

// Canvas must be 275x116 (normal) or 275x14 (shade).
void render_main(Canvas& c, const Skin& skin, const MainState& st);

} // namespace wsz
