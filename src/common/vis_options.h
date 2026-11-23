#pragma once

namespace wsz {

enum class VisMode { spectrum = 0, oscilloscope = 1, off = 2 };

// Winamp's classic vis options; defaults are Winamp's.
struct VisOptions {
    int analyzer = 0;        // 0 normal, 1 fire, 2 line
    bool thin_bands = false; // one band per column instead of 4-column groups
    bool peaks = true;
    int scope = 1;           // 0 dots, 1 lines, 2 solid
    int refresh = 2;         // draw every Nth falloff cycle (1..4)
    int falloff = 2;         // bar falloff, 0 slowest .. 4 fastest
    int peak_falloff = 1;    // peak falloff, 0 slowest .. 4 fastest
};

} // namespace wsz
