#include "stdafx.h"
#include "docking.h"
#include "config.h"

#include <algorithm>

namespace wsz::docking {
namespace {

std::vector<HWND> g_windows;
HWND g_leader = nullptr;
RECT g_leader_rect = {};
std::vector<HWND> g_group;
bool g_suppress = false; // programmatic moves: no snapping
HWND g_move_wnd = nullptr; // window being dragged by hand

enum class Side { below, above, right, left };
struct Attachment { HWND wnd, parent; Side side; int offset; };
std::vector<Attachment> g_layout; // begin_relayout .. end_relayout
bool g_relayout = false;
POINT g_grab = {};         // cursor offset from its top-left corner

RECT rect_of(HWND w) { RECT rc{}; GetWindowRect(w, &rc); return rc; }

bool overlaps(LONG a0, LONG a1, LONG b0, LONG b1) { return a0 < b1 && b0 < a1; }

bool touching(const RECT& a, const RECT& b) {
    const bool vert = (a.right == b.left || a.left == b.right) && overlaps(a.top, a.bottom, b.top, b.bottom);
    const bool horz = (a.bottom == b.top || a.top == b.bottom) && overlaps(a.left, a.right, b.left, b.right);
    return vert || horz;
}

// Snaps one coordinate: moves `pos` (edge at pos, other edge at pos + size) so that one of its edges
// lands on `line` when close enough. Returns true if snapped.
bool snap_axis(int& pos, int size, int line, int& best, int snap_distance) {
    const int d1 = line - pos, d2 = line - (pos + size);
    const int d = std::abs(d1) <= std::abs(d2) ? d1 : d2;
    if (std::abs(d) < snap_distance && std::abs(d) < std::abs(best)) { best = d; return true; }
    return false;
}

} // namespace

void add(HWND wnd) {
    if (std::find(g_windows.begin(), g_windows.end(), wnd) == g_windows.end()) g_windows.push_back(wnd);
}

void remove(HWND wnd) {
    g_windows.erase(std::remove(g_windows.begin(), g_windows.end(), wnd), g_windows.end());
    g_group.erase(std::remove(g_group.begin(), g_group.end(), wnd), g_group.end());
    if (g_leader == wnd) end_group_move();
    if (g_move_wnd == wnd) g_move_wnd = nullptr;
}

void snap(HWND wnd, WINDOWPOS* wp) {
    if ((wp->flags & SWP_NOMOVE) || g_suppress) return;
    // Shift inverts the docking setting while dragging, like Winamp.
    const bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
    if (config::snap.get() == shift) return;
    const int snap_distance = std::clamp(static_cast<int>(config::snap_distance.get()), 1, 99);
    const bool leading = g_leader == wnd;
    if (g_leader && !leading && std::find(g_group.begin(), g_group.end(), wnd) != g_group.end()) return; // moved by its leader
    const RECT cur = rect_of(wnd);
    const int w = (wp->flags & SWP_NOSIZE) ? cur.right - cur.left : wp->cx;
    const int h = (wp->flags & SWP_NOSIZE) ? cur.bottom - cur.top : wp->cy;

    // Moving rects at the proposed position: the window itself plus, for a group move, its docked windows.
    std::vector<RECT> moving = { { wp->x, wp->y, wp->x + w, wp->y + h } };
    if (leading) {
        const int dx = wp->x - cur.left, dy = wp->y - cur.top;
        for (HWND g : g_group) {
            RECT r = rect_of(g);
            OffsetRect(&r, dx, dy);
            moving.push_back(r);
        }
    }

    int best_x = snap_distance, best_y = snap_distance;
    for (const RECT& m : moving) {
        int x = m.left, y = m.top;
        const int mw = m.right - m.left, mh = m.bottom - m.top;
        MONITORINFO mi = {};
        mi.cbSize = sizeof(mi);
        if (GetMonitorInfoW(MonitorFromRect(&m, MONITOR_DEFAULTTONEAREST), &mi)) {
            snap_axis(x, mw, mi.rcWork.left, best_x, snap_distance);
            snap_axis(x, mw, mi.rcWork.right, best_x, snap_distance);
            snap_axis(y, mh, mi.rcWork.top, best_y, snap_distance);
            snap_axis(y, mh, mi.rcWork.bottom, best_y, snap_distance);
        }
        for (HWND other : g_windows) {
            if (other == wnd || !IsWindowVisible(other) || IsIconic(other)) continue;
            if (leading && std::find(g_group.begin(), g_group.end(), other) != g_group.end()) continue;
            const RECT o = rect_of(other);
            // Only snap to windows that are near on the other axis.
            if (overlaps(m.top - snap_distance, m.bottom + snap_distance, o.top, o.bottom)) {
                snap_axis(x, mw, o.left, best_x, snap_distance);
                snap_axis(x, mw, o.right, best_x, snap_distance);
            }
            if (overlaps(m.left - snap_distance, m.right + snap_distance, o.left, o.right)) {
                snap_axis(y, mh, o.top, best_y, snap_distance);
                snap_axis(y, mh, o.bottom, best_y, snap_distance);
            }
        }
    }
    if (std::abs(best_x) < snap_distance) wp->x += best_x;
    if (std::abs(best_y) < snap_distance) wp->y += best_y;
}

// Like Winamp, closed (hidden) windows touching the group move with it, so they reopen docked;
// only open windows extend the chain further.
std::vector<HWND> docked_to(HWND leader) {
    std::vector<HWND> group;
    std::vector<RECT> rects = { rect_of(leader) };
    bool grew = true;
    while (grew) {
        grew = false;
        for (HWND w : g_windows) {
            if (w == leader || IsIconic(w)) continue;
            if (std::find(group.begin(), group.end(), w) != group.end()) continue;
            const RECT r = rect_of(w);
            for (auto& g : rects) {
                if (touching(r, g)) {
                    group.push_back(w);
                    if (IsWindowVisible(w)) { rects.push_back(r); grew = true; }
                    break;
                }
            }
        }
    }
    return group;
}

void begin_group_move(HWND leader) {
    g_leader = leader;
    g_leader_rect = rect_of(leader);
    g_group = docked_to(leader);
}

void group_moved(HWND leader) {
    if (leader != g_leader || g_group.empty()) return;
    const RECT now = rect_of(leader);
    const int dx = now.left - g_leader_rect.left, dy = now.top - g_leader_rect.top;
    g_leader_rect = now;
    if (!dx && !dy) return;
    HDWP dwp = BeginDeferWindowPos(static_cast<int>(g_group.size()));
    for (HWND w : g_group) {
        const RECT r = rect_of(w);
        if (dwp) dwp = DeferWindowPos(dwp, w, nullptr, r.left + dx, r.top + dy, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    }
    if (dwp) EndDeferWindowPos(dwp);
}

void end_group_move() {
    g_leader = nullptr;
    g_group.clear();
}

void begin_move(HWND wnd, bool group) {
    POINT pt{};
    GetCursorPos(&pt);
    const RECT rc = rect_of(wnd);
    g_move_wnd = wnd;
    g_grab = { pt.x - rc.left, pt.y - rc.top };
    if (group) begin_group_move(wnd);
    SetCapture(wnd);
}

bool moving(HWND wnd) { return wnd && g_move_wnd == wnd; }

void move_to_cursor(HWND wnd) {
    if (!moving(wnd)) return;
    POINT pt{};
    GetCursorPos(&pt);
    SetWindowPos(wnd, nullptr, pt.x - g_grab.x, pt.y - g_grab.y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    group_moved(wnd);
}

bool end_move(HWND wnd) {
    if (!moving(wnd)) return false;
    g_move_wnd = nullptr;
    if (g_leader == wnd) end_group_move();
    if (GetCapture() == wnd) ReleaseCapture();
    return true;
}

void begin_relayout(HWND root) {
    g_layout.clear();
    g_relayout = true;
    // Same walk as docked_to(), remembering which window each one was found against.
    std::vector<HWND> placed = { root };
    std::vector<HWND> chain = { root }; // visible ones extend it
    bool grew = true;
    while (grew) {
        grew = false;
        for (HWND w : g_windows) {
            if (IsIconic(w) || std::find(placed.begin(), placed.end(), w) != placed.end()) continue;
            const RECT r = rect_of(w);
            for (HWND p : chain) {
                const RECT pr = rect_of(p);
                if (!touching(r, pr)) continue;
                Attachment a{ w, p, Side::below, 0 };
                if (r.top == pr.bottom) a = { w, p, Side::below, r.left - pr.left };
                else if (r.bottom == pr.top) a = { w, p, Side::above, r.left - pr.left };
                else if (r.left == pr.right) a = { w, p, Side::right, r.top - pr.top };
                else a = { w, p, Side::left, r.top - pr.top };
                g_layout.push_back(a);
                placed.push_back(w);
                if (IsWindowVisible(w)) { chain.push_back(w); grew = true; }
                break;
            }
        }
    }
}

bool relayout_active() { return g_relayout; }

void end_relayout() {
    g_relayout = false;
    g_suppress = true;
    for (const Attachment& a : g_layout) {
        if (!IsWindow(a.wnd) || !IsWindow(a.parent)) continue;
        const RECT p = rect_of(a.parent), r = rect_of(a.wnd);
        const int w = r.right - r.left, h = r.bottom - r.top;
        POINT pos{};
        switch (a.side) {
        case Side::below: pos = { p.left + a.offset, p.bottom }; break;
        case Side::above: pos = { p.left + a.offset, p.top - h }; break;
        case Side::right: pos = { p.right, p.top + a.offset }; break;
        case Side::left: pos = { p.left - w, p.top + a.offset }; break;
        }
        if (pos.x != r.left || pos.y != r.top) SetWindowPos(a.wnd, nullptr, pos.x, pos.y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    }
    g_suppress = false;
    g_layout.clear();
}

void resized(HWND wnd, const RECT& before, const RECT& after, const std::vector<HWND>& docked_before) {
    if (g_relayout) return;
    const int dy = (after.bottom - after.top) - (before.bottom - before.top);
    const int dx = (after.right - after.left) - (before.right - before.left);
    if (!dx && !dy) return;
    g_suppress = true;
    for (HWND w : docked_before) {
        if (w == wnd) continue;
        const RECT r = rect_of(w);
        int x = r.left, y = r.top;
        if (r.top >= before.bottom) y += dy;
        if (r.left >= before.right) x += dx;
        if (x != r.left || y != r.top) SetWindowPos(w, nullptr, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    }
    g_suppress = false;
}

} // namespace wsz::docking
