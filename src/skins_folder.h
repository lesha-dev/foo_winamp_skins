#pragma once

#include <string>
#include <vector>

namespace wsz::skins_folder {

struct Entry {
    std::wstring name; // display name: file name without extension, or directory name
    std::wstring path; // full path
};

// <profile>\winamp-skins, created on first use.
std::wstring path();

// .wsz/.zip files and unpacked skin directories (containing main.bmp) in the folder, naturally sorted.
std::vector<Entry> list();

void open_in_explorer();

} // namespace wsz::skins_folder
