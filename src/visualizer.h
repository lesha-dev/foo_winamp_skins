#pragma once

#include "canvas.h"
#include "skin.h"
#include "vis_options.h"

#include <array>
#include <chrono>
#include <utility>

namespace wsz {

// Main-window visualizer: pulls PCM from fb2k's visualisation stream and draws it with the skin's
// viscolor.txt palette the way Winamp 2/5's classic visualizer does, with Winamp's vis options.
class Visualizer {
public:
    static constexpr int bar_count = 19;
    static constexpr int width = 76, height = 16;           // normal mode
    static constexpr int shade_width = 38, shade_height = 5; // windowshade mode
    static constexpr int points = 75;                        // Winamp's analyser resolution

    Visualizer();
    ~Visualizer();

    VisMode mode() const { return m_mode; }
    void set_mode(VisMode m);
    const VisOptions& options() const { return m_opt; }
    void set_options(const VisOptions& o);

    // Advances the animation. Returns false once there is nothing left to animate (stopped and fully decayed).
    bool tick(bool playing);
    bool take_redraw() { return std::exchange(m_redraw, false); } // a draw cycle happened since the last call
    void reset();
    void freeze() { m_last_tick = {}; m_pending = 0; } // keeps the picture; the next tick starts a fresh interval
    void release(); // drops the vis stream (window teardown)

    void render(Canvas& c, const Skin& skin, layout::Pos pos, bool shade) const;

private:
    struct Falloff {               // per-column bar/peak state, Winamp units
        std::array<int, points> bar{};      // height * 16
        std::array<int, points> peak{};     // height * 256
        std::array<float, points> peak_speed{};
        std::array<int, points> shown{};    // height drawn this frame
        std::array<int, points> peak_shown{}; // peak drawn this frame (before this cycle's decay), * 256
    };

    bool fetch_spectrum(std::array<int, points + 4>& out);
    bool fetch_scope(std::array<int, points>& out);
    void step(Falloff& f, int column, int v, bool draw);
    void ensure_stream();
    int bar_color(int v) const;

    VisMode m_mode = VisMode::spectrum;
    VisOptions m_opt;
    int m_cycle = 0;    // cycles since the last draw cycle
    bool m_redraw = false;
    visualisation_stream_v2::ptr m_stream;
    audio_chunk_impl m_chunk;

    Falloff m_normal, m_shade;
    std::array<int, points + 4> m_spec{};      // 75 analyser values (0..255) + 4 zero pads
    std::array<int, points> m_scope{};          // 75 scope values (-16..16)
    bool m_scope_valid = false;
    std::chrono::steady_clock::time_point m_last_tick{};
    double m_pending = 0;                       // seconds not yet consumed by falloff cycles
};

} // namespace wsz
