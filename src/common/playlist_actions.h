#pragma once

#include <functional>

// Winamp's playlist editor button menus (Add, Rem, Sel, Misc, List) on foobar2000's active playlist.
namespace wsz::playlist_actions {

enum class Menu { add, rem, sel, misc, list };

// Items per menu, top to bottom, as Winamp shows them.
int item_count(Menu m);
const wchar_t* item_label(Menu m, int item);

// Shows a popup menu at `pt` and returns the chosen command (0 = none). Lets the caller apply its cursor.
using TrackMenu = std::function<UINT(HMENU menu, POINT pt)>;

// Runs one item; items that open a further menu (Remove misc, Sort list, Misc options) use `track`
// at the cursor, owned by `owner`.
void run(Menu m, int item, HWND owner, const TrackMenu& track);

} // namespace wsz::playlist_actions
