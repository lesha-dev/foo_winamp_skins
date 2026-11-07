#pragma once

#include <string>
#include <vector>

namespace wsz::skins_folder {

struct Entry {
    std::wstring name; // display name: file name without extension, or directory name
    std::wstring path; // full path
};

// Custom folder chosen in Preferences, else <profile>\winamp-skins (created on first use).
std::wstring path();

// Store a new folder; the default folder is stored as empty.
void set_path(const std::wstring& dir);

// Folder picker starting at `start`; returns true and sets `out` if a folder was chosen.
bool browse(HWND owner, const std::wstring& start, std::wstring& out);

// .wsz/.zip files and unpacked skin directories (containing main.bmp) in the folder, naturally sorted.
std::vector<Entry> list();
std::vector<Entry> list(const std::wstring& dir);

// A random skin from the folder, other than `exclude` when there's a choice; empty if the folder has none.
std::wstring random(const std::wstring& exclude);

void open_in_explorer();
void open_in_explorer(const std::wstring& dir);

} // namespace wsz::skins_folder
