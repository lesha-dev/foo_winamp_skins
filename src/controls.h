#pragma once

#include "skin_layout.h"

namespace wsz {

// Clickable elements of the main window.
enum class Control {
    none,
    options, minimize, shade, close,
    clutter_o, clutter_a, clutter_i, clutter_d, clutter_v,
    prev, play, pause, stop, next, eject,
    shuffle, repeat, eq, pl,
    posbar, volume, balance,
    time, vis, about, marquee,
};

struct ControlRect { Control id; int x, y, w, h; };

// Hit rects in skin coordinates. Order matters only where rects overlap (first match wins).
inline constexpr ControlRect normal_controls[] = {
    { Control::options,   6,   3,  9,  9 },
    { Control::minimize,  244, 3,  9,  9 },
    { Control::shade,     254, 3,  9,  9 },
    { Control::close,     264, 3,  9,  9 },
    { Control::clutter_o, 10, 25,  8,  8 },
    { Control::clutter_a, 10, 33,  8,  7 },
    { Control::clutter_i, 10, 40,  8,  7 },
    { Control::clutter_d, 10, 47,  8,  8 },
    { Control::clutter_v, 10, 55,  8,  7 },
    { Control::time,      36, 26, 63, 13 },
    { Control::marquee,  117, 24, 150, 12 },
    { Control::vis,       24, 43, 76, 16 },
    { Control::volume,   107, 57, 68, 13 },
    { Control::balance,  177, 57, 38, 13 },
    { Control::eq,       219, 58, 23, 12 },
    { Control::pl,       242, 58, 23, 12 },
    { Control::posbar,    16, 72, 248, 10 },
    { Control::prev,      16, 88, 23, 18 },
    { Control::play,      39, 88, 23, 18 },
    { Control::pause,     62, 88, 23, 18 },
    { Control::stop,      85, 88, 23, 18 },
    { Control::next,     108, 88, 22, 18 },
    { Control::eject,    136, 89, 22, 16 },
    { Control::shuffle,  164, 89, 47, 15 },
    { Control::repeat,   210, 89, 28, 15 },
    { Control::about,    253, 91, 13, 15 },
};

inline constexpr ControlRect shade_controls[] = {
    { Control::options,   6,   3,  9,  9 },
    { Control::minimize,  244, 3,  9,  9 },
    { Control::shade,     254, 3,  9,  9 },
    { Control::close,     264, 3,  9,  9 },
    { Control::time,      127, 4, 25,  6 },
    { Control::vis,       79,  5, 38,  5 },
    { Control::prev,      169, 2,  7, 10 },
    { Control::play,      176, 2, 10, 10 },
    { Control::pause,     186, 2,  9, 10 },
    { Control::stop,      195, 2,  9, 10 },
    { Control::next,      204, 2, 10, 10 },
    { Control::eject,     215, 2, 10, 10 },
    { Control::posbar,    226, 4, 17,  7 },
};

template <size_t N>
const ControlRect* hit_test(const ControlRect (&rects)[N], int x, int y) {
    for (auto& r : rects)
        if (x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h) return &r;
    return nullptr;
}

template <size_t N>
const ControlRect* find_control(const ControlRect (&rects)[N], Control id) {
    for (auto& r : rects)
        if (r.id == id) return &r;
    return nullptr;
}

} // namespace wsz
