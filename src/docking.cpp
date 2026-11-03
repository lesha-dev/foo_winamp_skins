#include "stdafx.h"
#include "docking.h"

#include <algorithm>

namespace wsz::docking {
namespace {

constexpr int snap_distance = 10;

std::vector<HWND> g_windows;
HWND g_leader = nullptr;
RECT g_leader_rect = {};
std::vector<HWND> g_group;
bool g_suppress = false; // programmatic moves: no snapping

RECT rect_of(HWND w) { RECT rc{}; GetWindowRect(w, &rc); return rc; }

bool overlaps(LONG a0, LONG a1, LONG b0, LONG b1) { return a0 < b1 && b0 < a1; }

bool touching(const RECT& a, const RECT& b) {
    const bool vert = (a.right == b.left || a.left == b.right) && overlaps(a.top, a.bottom, b.top, b.bottom);
    const bool horz = (a.bottom == b.top || a.top == b.bottom) && overlaps(a.left, a.right, b.left, b.right);
    return vert || horz;
}

// Snaps one coordinate: moves `pos` (edge at pos, other edge at pos + size) so that one of its edges
// lands on `line` when close enough. Returns true if snapped.
bool snap_axis(int& pos, int size, int line, int& best) {
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
}

void snap(HWND wnd, WINDOWPOS* wp) {
    if ((wp->flags & SWP_NOMOVE) || g_suppress) return;
    if (g_leader && std::find(g_group.begin(), g_group.end(), wnd) != g_group.end()) return; // moved by its leader
    const RECT cur = rect_of(wnd);
    const int w = (wp->flags & SWP_NOSIZE) ? cur.right - cur.left : wp->cx;
    const int h = (wp->flags & SWP_NOSIZE) ? cur.bottom - cur.top : wp->cy;
    RECT target = { wp->x, wp->y, wp->x + w, wp->y + h };

    int best_x = snap_distance, best_y = snap_distance;
    MONITORINFO mi = {};
    mi.cbSize = sizeof(mi);
    if (GetMonitorInfoW(MonitorFromRect(&target, MONITOR_DEFAULTTONEAREST), &mi)) {
        snap_axis(wp->x, w, mi.rcWork.left, best_x);
        snap_axis(wp->x, w, mi.rcWork.right, best_x);
        snap_axis(wp->y, h, mi.rcWork.top, best_y);
        snap_axis(wp->y, h, mi.rcWork.bottom, best_y);
    }
    for (HWND other : g_windows) {
        if (other == wnd || !IsWindowVisible(other) || IsIconic(other)) continue;
        if (g_leader == wnd && std::find(g_group.begin(), g_group.end(), other) != g_group.end()) continue;
        const RECT o = rect_of(other);
        // Only snap to windows that are near on the other axis.
        if (overlaps(target.top - snap_distance, target.bottom + snap_distance, o.top, o.bottom)) {
            snap_axis(wp->x, w, o.left, best_x);
            snap_axis(wp->x, w, o.right, best_x);
        }
        if (overlaps(target.left - snap_distance, target.right + snap_distance, o.left, o.right)) {
            snap_axis(wp->y, h, o.top, best_y);
            snap_axis(wp->y, h, o.bottom, best_y);
        }
    }
    if (std::abs(best_x) < snap_distance) wp->x += best_x;
    if (std::abs(best_y) < snap_distance) wp->y += best_y;
}

std::vector<HWND> docked_to(HWND leader) {
    std::vector<HWND> group;
    std::vector<RECT> rects = { rect_of(leader) };
    bool grew = true;
    while (grew) {
        grew = false;
        for (HWND w : g_windows) {
            if (w == leader || !IsWindowVisible(w) || IsIconic(w)) continue;
            if (std::find(group.begin(), group.end(), w) != group.end()) continue;
            const RECT r = rect_of(w);
            for (auto& g : rects) {
                if (touching(r, g)) {
                    group.push_back(w);
                    rects.push_back(r);
                    grew = true;
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

void resized(HWND wnd, const RECT& before, const RECT& after, const std::vector<HWND>& docked_before) {
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
