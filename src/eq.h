#pragma once

#include <array>
#include <string>
#include <vector>

// Winamp-style 10-band equalizer front end for foobar2000's built-in Equalizer (18 bands, no preamp):
// our values are mapped onto it (preamp added to every band) and written into the DSP chain.
namespace wsz::eq {

constexpr int band_count = 10;
constexpr std::array<double, band_count> band_hz = { 70, 180, 320, 600, 1000, 3000, 6000, 12000, 14000, 16000 };
constexpr double max_db = 12.0;

// Slider values 0..100, 50 = 0 dB, 100 = +12 dB.
struct Values {
    int preamp = 50;
    std::array<int, band_count> bands = { 50, 50, 50, 50, 50, 50, 50, 50, 50, 50 };
};

inline double to_db(int v) { return (v - 50) / 50.0 * max_db; }

Values get();
void set(const Values& v);      // persists; updates the built-in EQ when enabled. Main thread only.

bool enabled();                 // = built-in Equalizer present in the DSP chain
void set_enabled(bool on);      // adds/removes the built-in Equalizer. Main thread only.
void sync();                    // reads the current chain state (startup)

// Slider drag: hints playback to apply changes with low latency.
void begin_edit();
void end_edit();

// foobar2000 .feq preset files (text, one dB value per line for its 18 bands). Read and written
// through fb2k's Equalizer bands directly (exact round trip); our 10 sliders follow from them.
// load_feq applies the preset itself. Main thread only.
bool load_feq(const char* path_utf8);
bool save_feq(const char* path_utf8);

} // namespace wsz::eq
