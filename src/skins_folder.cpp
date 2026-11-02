#include "stdafx.h"
#include "skins_folder.h"

#include <shellapi.h>
#include <shlwapi.h>

namespace wsz::skins_folder {
namespace {

bool has_ext(const std::wstring& name, const wchar_t* ext) {
    const size_t n = wcslen(ext);
    return name.size() > n && _wcsicmp(name.c_str() + name.size() - n, ext) == 0;
}

bool is_skin_dir(const std::wstring& dir) {
    const std::wstring main = dir + L"\\main.bmp";
    return GetFileAttributesW(main.c_str()) != INVALID_FILE_ATTRIBUTES;
}

} // namespace

std::wstring path() {
    const pfc::string8 native = filesystem::g_get_native_path(core_api::pathInProfile("winamp-skins"));
    std::wstring dir = pfc::stringcvt::string_wide_from_utf8(native).get_ptr();
    CreateDirectoryW(dir.c_str(), nullptr);
    return dir;
}

std::vector<Entry> list() {
    std::vector<Entry> out;
    const std::wstring dir = path();
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((dir + L"\\*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return out;
    do {
        const std::wstring name = fd.cFileName;
        if (name == L"." || name == L"..") continue;
        const std::wstring full = dir + L"\\" + name;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            if (is_skin_dir(full)) out.push_back({ name, full });
        } else if (has_ext(name, L".wsz") || has_ext(name, L".zip")) {
            out.push_back({ name.substr(0, name.size() - 4), full });
        }
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    std::sort(out.begin(), out.end(), [](const Entry& a, const Entry& b) { return StrCmpLogicalW(a.name.c_str(), b.name.c_str()) < 0; });
    return out;
}

void open_in_explorer() {
    const std::wstring dir = path();
    ShellExecuteW(nullptr, L"open", dir.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

} // namespace wsz::skins_folder
