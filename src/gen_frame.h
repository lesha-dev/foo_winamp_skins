#pragma once

#include "canvas.h"
#include "skin.h"

namespace wsz {

// Winamp's generic ("embedded") window frame, drawn from gen.bmp. Skins without gen.bmp get a frame
// built from the playlist editor's pieces (pledit.bmp) instead.
struct GenFrame {
    // Winamp's minimum is 275x116; smaller windows show the frame's four corners, cut to fit.
    static constexpr int min_width = 30, min_height = 30, step_w = 25, step_h = 29;
    static constexpr int full_width = 275, full_height = 116;

    // Content area insets for the skin's frame.
    struct Insets { int left, top, right, bottom; };
    static Insets insets(const Skin& skin);

    static void render(Canvas& c, const Skin& skin, int w, int h, const char* title, bool active, bool close_pressed);
};

} // namespace wsz
