#pragma once

#include "canvas.h"
#include "skin.h"

namespace wsz {

// Winamp's generic ("embedded") window frame, drawn from gen.bmp. Skins without gen.bmp get a frame
// built from the playlist editor's pieces (pledit.bmp) instead.
struct GenFrame {
    static constexpr int min_width = 275, min_height = 116, step_w = 25, step_h = 29;

    // Content area insets for the skin's frame.
    struct Insets { int left, top, right, bottom; };
    static Insets insets(const Skin& skin);

    static void render(Canvas& c, const Skin& skin, int w, int h, const char* title, bool active, bool close_pressed);
};

} // namespace wsz
