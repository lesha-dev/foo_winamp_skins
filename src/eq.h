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

struct Preset {
    std::string name;
    Values values;
};
// Winamp's built-in presets.
const std::vector<Preset>& presets();

// .eqf (Winamp EQ preset) import: returns false if the file isn't one. Uses the first preset in it.
bool load_eqf(const char* path_utf8, Values& out, std::string& name);

} // namespace wsz::eq
