#pragma once

#include <memory>
#include <vector>

namespace wsz::cui {

// Columns UI panels (the Columns UI SDK's window services). Without Columns UI installed the list is
// empty unless another component provides such panels.
struct PanelInfo {
    GUID guid;
    pfc::string8 name;
    pfc::string8 category;
    bool playlist = false; // a playlist view: lives in the playlist editor
};
std::vector<PanelInfo> list_panels();
bool find_panel(const GUID& guid, PanelInfo* out = nullptr);

// Colours for list panels instead of Columns UI's.
struct ListColours {
    COLORREF text = 0;
    COLORREF background = 0;
    COLORREF selection_text = 0;
    COLORREF selection_background = 0;
    bool operator==(const ListColours&) const = default;
};

// One panel instance as a child window. Its colours and fonts are Columns UI's own (Preferences >
// Columns UI > Colours and fonts), as are global panel settings such as the playlist view's columns;
// list panels may take their font and colours from the host instead (set_list_font, set_list_colours).
class Panel {
public:
    explicit Panel(const GUID& guid);
    ~Panel();
    Panel(const Panel&) = delete;
    Panel& operator=(const Panel&) = delete;

    // Instantiates the panel inside `parent` with its saved configuration (empty: defaults).
    bool create(HWND parent, const std::vector<uint8_t>& config);
    HWND hwnd() const;
    // Font for list text instead of Columns UI's (null = Columns UI's), in Columns UI versions that let
    // hosts override panel fonts; older versions keep their own.
    void set_list_font(HFONT font);
    // Colours for list panels instead of Columns UI's (null = Columns UI's), in the same Columns UI versions.
    void set_list_colours(const ListColours* colours);
    // The panel's configuration, if it is alive.
    bool config(std::vector<uint8_t>& out) const;
    void release();

private:
    struct Impl;
    std::unique_ptr<Impl> m;
};

} // namespace wsz::cui
