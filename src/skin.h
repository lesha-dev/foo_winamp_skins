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

// Skinned mouse cursors, in Winamp's order (one .cur/.ani file each).
enum class SkinCursor {
    // main window
    volbal, posbar, winbut, min, close, mainmenu, titlebar, songname, normal,
    // main window, windowshade
    ws_winbut, ws_min, ws_posbar, ws_close, ws_mainmenu, ws_normal,
    // playlist
    pl_winbut, pl_close, pl_titlebar, pl_vscroll, pl_size, pl_normal,
    // playlist, windowshade
    plws_winbut, plws_close, plws_size, plws_normal,
    // equalizer
    eq_slider, eq_close, eq_titlebar, eq_normal,
    count_
};

// True when (x, y) lies in the inclusive rectangle, like Winamp's cursor hit tests.
inline bool in_rect(POINT p, int l, int t, int r, int b) { return p.x >= l && p.x <= r && p.y >= t && p.y <= b; }

class Skin;
// Sets the skin's cursor (or the arrow when the skin has none or skinned cursors are off).
void set_skin_cursor(const Skin* skin, SkinCursor c);

class Skin {
public:
    ~Skin();
    Skin(const Skin&) = delete;
    Skin& operator=(const Skin&) = delete;

    // Loads a .wsz/.zip archive or an unpacked skin directory. Throws std::runtime_error on fatal errors;
    // missing individual files are tolerated (left empty / defaults).
    static std::unique_ptr<Skin> load(const char* path_utf8);

    const Image& bitmap(SkinBitmap b) const { return m_bitmaps[static_cast<size_t>(b)]; }
    const std::array<COLORREF, 24>& vis_colors() const { return m_vis_colors; }
    const PlaylistColors& playlist_colors() const { return m_pl_colors; }
    const SkinRegions& regions() const { return m_regions; }
    const std::string& path() const { return m_path; }
    const std::vector<std::string>& warnings() const { return m_warnings; }
    // Skin author's notes: the readme-like .txt shipped in the skin (UTF-8, may be empty).
    const std::string& readme() const { return m_readme; }
    // Cursor shipped by the skin, or null.
    HCURSOR cursor(SkinCursor c) const { return m_cursors[static_cast<size_t>(c)]; }

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
    std::string m_readme;
    std::array<HCURSOR, static_cast<size_t>(SkinCursor::count_)> m_cursors{};
};

} // namespace wsz
