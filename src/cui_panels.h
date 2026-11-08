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

// One panel instance as a child window. Its colours and fonts are Columns UI's own (Preferences >
// Columns UI > Colours and fonts), as are global panel settings such as the playlist view's columns.
class Panel {
public:
    explicit Panel(const GUID& guid);
    ~Panel();
    Panel(const Panel&) = delete;
    Panel& operator=(const Panel&) = delete;

    // Instantiates the panel inside `parent` with its saved configuration (empty: defaults).
    bool create(HWND parent, const std::vector<uint8_t>& config);
    HWND hwnd() const;
    // The panel's configuration, if it is alive.
    bool config(std::vector<uint8_t>& out) const;
    void release();

private:
    struct Impl;
    std::unique_ptr<Impl> m;
};

} // namespace wsz::cui
