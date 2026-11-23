#include "common/stdafx.h"
#include "common/playlist_actions.h"

namespace wsz::playlist_actions {

namespace {

const wchar_t* const labels[5][4] = {
    {L"Add URL", L"Add folder", L"Add file(s)", nullptr},
    {L"Remove misc", L"Remove all", L"Crop", L"Remove selected"},
    {L"Invert selection", L"Select none", L"Select all", nullptr},
    {L"Sort list", L"File info", L"Misc options", nullptr},
    {L"New list", L"Save list", L"Load list", nullptr},
};

} // namespace

int item_count(Menu m) { return m == Menu::rem ? 4 : 3; }

const wchar_t* item_label(Menu m, int item) {
    return item >= 0 && item < item_count(m) ? labels[static_cast<int>(m)][item] : L"";
}

void run(Menu menu, int item, HWND owner, const TrackMenu& track) {
    auto pm = playlist_manager::get();
    const size_t n = pm->activeplaylist_get_item_count();
    POINT pt;
    GetCursorPos(&pt);
    switch (menu) {
    case Menu::add:
        if (item == 0) standard_commands::main_add_location();
        else if (item == 1) standard_commands::main_add_directory();
        else standard_commands::main_add_files();
        break;
    case Menu::rem:
        if (item == 0) {
            enum : UINT { id_dead = 1, id_dupes };
            HMENU m = CreatePopupMenu();
            AppendMenuW(m, MF_STRING, id_dead, L"Remove &dead items");
            AppendMenuW(m, MF_STRING, id_dupes, L"Remove d&uplicates");
            const UINT cmd = track(m, pt);
            DestroyMenu(m);
            if (cmd == id_dead) standard_commands::main_remove_dead_entries();
            else if (cmd == id_dupes) standard_commands::main_remove_duplicates();
        } else if (item == 1) pm->activeplaylist_clear();
        else if (item == 2) pm->activeplaylist_remove_selection(true);
        else pm->activeplaylist_remove_selection(false);
        break;
    case Menu::sel:
        if (item == 0) {
            pfc::bit_array_bittable sel(n);
            pm->activeplaylist_get_selection_mask(sel);
            pm->activeplaylist_set_selection(pfc::bit_array_true(), pfc::bit_array_not(sel));
        } else if (item == 1) pm->activeplaylist_clear_selection();
        else pm->activeplaylist_set_selection(pfc::bit_array_true(), pfc::bit_array_true());
        break;
    case Menu::misc:
        if (item == 0) {
            enum : UINT { id_title = 1, id_file, id_path, id_reverse, id_random };
            HMENU m = CreatePopupMenu();
            AppendMenuW(m, MF_STRING, id_title, L"Sort list by &title");
            AppendMenuW(m, MF_STRING, id_file, L"Sort list by &filename");
            AppendMenuW(m, MF_STRING, id_path, L"Sort list by &path and filename");
            AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
            AppendMenuW(m, MF_STRING, id_reverse, L"&Reverse list");
            AppendMenuW(m, MF_STRING, id_random, L"R&andomize list");
            const UINT cmd = track(m, pt);
            DestroyMenu(m);
            if (cmd == id_title) pm->activeplaylist_sort_by_format("%title%", false);
            else if (cmd == id_file) pm->activeplaylist_sort_by_format("%filename_ext%", false);
            else if (cmd == id_path) pm->activeplaylist_sort_by_format("%path%", false);
            else if (cmd == id_random) pm->activeplaylist_sort_by_format(nullptr, false);
            else if (cmd == id_reverse && n > 1) {
                std::vector<size_t> order(n);
                for (size_t i = 0; i < n; ++i) order[i] = n - 1 - i;
                pm->activeplaylist_reorder_items(order.data(), n);
            }
        } else if (item == 1) {
            metadb_handle_list items;
            pm->activeplaylist_get_selected_items(items);
            if (items.get_count()) standard_commands::context_file_properties(items);
        } else {
            contextmenu_manager::win32_run_menu_context_playlist(owner, &pt);
        }
        break;
    case Menu::list:
        if (item == 0) standard_commands::main_create_playlist();
        else if (item == 1) standard_commands::main_save_playlist();
        else standard_commands::main_load_playlist();
        break;
    }
}

} // namespace wsz::playlist_actions
