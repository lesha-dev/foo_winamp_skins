#pragma once

#include "canvas.h"
#include "skin.h"

#include <array>

namespace wsz {

enum class VisMode { spectrum = 0, oscilloscope = 1, off = 2 };

// Main-window visualizer: pulls spectrum/PCM from fb2k's visualisation stream and draws it
// with the skin's viscolor.txt palette, Winamp-style (19 bars + peaks, or line oscilloscope).
class Visualizer {
public:
    static constexpr int bar_count = 19;
    static constexpr int width = 76, height = 16;           // normal mode
    static constexpr int shade_width = 38, shade_height = 5; // windowshade mode

    Visualizer();
    ~Visualizer();

    VisMode mode() const { return m_mode; }
    void set_mode(VisMode m);

    // Advances one frame. Returns false once there is nothing left to animate (stopped and fully decayed).
    bool tick(bool playing);
    void reset();
    void release(); // drops the vis stream (window teardown)

    void render(Canvas& c, const Skin& skin, layout::Pos pos, bool shade) const;

private:
    void update_spectrum(bool have_data);
    void update_oscilloscope(bool have_data);
    void ensure_stream();

    VisMode m_mode = VisMode::spectrum;
    visualisation_stream_v2::ptr m_stream;
    audio_chunk_impl m_chunk;

    std::array<float, bar_count> m_bars{};       // 0..1
    std::array<float, bar_count> m_peaks{};      // 0..1
    std::array<int, bar_count> m_peak_frames{};
    std::array<float, width> m_osc{};            // -1..1
    bool m_osc_valid = false;
};

} // namespace wsz
