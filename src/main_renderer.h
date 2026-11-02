#pragma once

#include "canvas.h"
#include "skin.h"

namespace wsz {

enum class PlayState { stopped, playing, paused };

// Everything the main window needs to draw one frame. Filled by MainWindow from fb2k state.
struct MainState {
    bool active = true;          // window focused -> highlighted titlebar
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
};

void render_main(Canvas& c, const Skin& skin, const MainState& st);

} // namespace wsz
