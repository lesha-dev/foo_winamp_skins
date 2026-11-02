#pragma once

#include "image.h"

#include <array>
#include <map>
#include <optional>
#include <string>

namespace wsz {

enum class SkinBitmap {
    main, titlebar, cbuttons, shufrep, posbar, volume, balance, monoster, playpaus,
    numbers, nums_ex, text, eqmain, eq_ex, pledit, gen, genex, video,
    count_
};

struct RegionPolygon {
    std::vector<POINT> points;
};

// Parsed region.txt: section name (lowercase, e.g. "normal", "windowshade", "equalizer") -> polygons.
using SkinRegions = std::map<std::string, std::vector<RegionPolygon>>;

struct PlaylistColors {
    COLORREF normal = RGB(0, 255, 0);
    COLORREF current = RGB(255, 255, 255);
    COLORREF normal_bg = RGB(0, 0, 0);
    COLORREF selected_bg = RGB(0, 0, 198);
    std::string font = "Arial";
};

class Skin {
public:
    // Loads a .wsz/.zip archive or an unpacked skin directory. Throws std::runtime_error on fatal errors;
    // missing individual files are tolerated (left empty / defaults).
    static std::unique_ptr<Skin> load(const char* path_utf8);

    const Image& bitmap(SkinBitmap b) const { return m_bitmaps[static_cast<size_t>(b)]; }
    const std::array<COLORREF, 24>& vis_colors() const { return m_vis_colors; }
    const PlaylistColors& playlist_colors() const { return m_pl_colors; }
    const SkinRegions& regions() const { return m_regions; }
    const std::string& path() const { return m_path; }
    const std::vector<std::string>& warnings() const { return m_warnings; }

private:
    Skin();

    // name: lowercase basename -> raw file contents
    using FileMap = std::map<std::string, std::vector<uint8_t>>;
    void load_from(FileMap& files);

    std::string m_path;
    std::array<Image, static_cast<size_t>(SkinBitmap::count_)> m_bitmaps;
    std::array<COLORREF, 24> m_vis_colors;
    PlaylistColors m_pl_colors;
    SkinRegions m_regions;
    std::vector<std::string> m_warnings;
};

} // namespace wsz
