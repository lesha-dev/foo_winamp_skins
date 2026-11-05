#pragma once

#include "controls.h"

#include <array>

struct ITaskbarList3;

namespace wsz {

class Skin;

// Thumbnail toolbar under the taskbar preview: Winamp's Previous, Play, Pause, Stop, Next drawn from the skin's
// cbuttons.bmp. Hidden while no skin is loaded.
class TaskbarButtons {
public:
    TaskbarButtons();
    ~TaskbarButtons();
    TaskbarButtons(const TaskbarButtons&) = delete;
    TaskbarButtons& operator=(const TaskbarButtons&) = delete;

    void attach(HWND wnd);
    void detach();
    void set_skin(const Skin* skin);

    // Returns true for the "TaskbarButtonCreated" message (Explorer started or restarted).
    bool on_message(UINT msg);
    // Control for a thumbnail button click (WM_COMMAND), else Control::none.
    Control on_command(WPARAM wp) const;

private:
    void add_buttons();
    void update_buttons();
    void build_icons();
    void free_icons();

    static constexpr size_t count = 5;
    HWND m_wnd = nullptr;
    ITaskbarList3* m_taskbar = nullptr;
    bool m_added = false;
    const Skin* m_skin = nullptr;
    std::array<HICON, count> m_icons{};
};

} // namespace wsz
