#pragma once

#include <vector>

// Winamp-style window docking: windows snap to screen edges and to each other, and windows docked
// to the main window (directly or through each other) move with it.
namespace wsz::docking {

void add(HWND wnd);
void remove(HWND wnd);

// WM_WINDOWPOSCHANGING: snaps a move to the work area and to the other visible windows.
void snap(HWND wnd, WINDOWPOS* wp);

// Visible windows docked to `leader`, transitively (leader excluded).
std::vector<HWND> docked_to(HWND leader);

// Main window drag: WM_ENTERSIZEMOVE -> begin, WM_MOVE -> moved, WM_EXITSIZEMOVE -> end.
void begin_group_move(HWND leader);
void group_moved(HWND leader);
void end_group_move();

// Window drag done by hand (windows are all client area, like Winamp; no system move loop).
// begin_move captures the mouse; with `group`, windows docked to `wnd` follow it.
void begin_move(HWND wnd, bool group);
bool moving(HWND wnd);
void move_to_cursor(HWND wnd);
// Returns true if `wnd` was being moved.
bool end_move(HWND wnd);

// Size change of a window (shade/double size/resize): docked windows below its old bottom edge
// shift by the height delta, those right of its old right edge by the width delta.
void resized(HWND wnd, const RECT& before, const RECT& after, const std::vector<HWND>& docked_before);

} // namespace wsz::docking
