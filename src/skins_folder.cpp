#include "stdafx.h"
#include "skins_folder.h"
#include "config.h"

#include <shobjidl.h>

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

static std::wstring default_path() {
    const pfc::string8 native = filesystem::g_get_native_path(core_api::pathInProfile("winamp-skins"));
    std::wstring dir = pfc::stringcvt::string_wide_from_utf8(native).get_ptr();
    CreateDirectoryW(dir.c_str(), nullptr);
    return dir;
}

std::wstring path() {
    const char* custom = config::skins_folder.get();
    if (*custom) return pfc::stringcvt::string_wide_from_utf8(custom).get_ptr();
    return default_path();
}

void set_path(const std::wstring& dir) {
    if (_wcsicmp(dir.c_str(), default_path().c_str()) == 0) config::skins_folder = "";
    else config::skins_folder = pfc::stringcvt::string_utf8_from_wide(dir.c_str()).get_ptr();
}

bool browse(HWND owner, const std::wstring& start_dir, std::wstring& out) {
    IFileOpenDialog* dlg = nullptr;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dlg)))) return false;
    bool chosen = false;
    DWORD opts = 0;
    dlg->GetOptions(&opts);
    dlg->SetOptions(opts | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
    dlg->SetTitle(L"Select skins folder");
    IShellItem* start = nullptr;
    if (SUCCEEDED(SHCreateItemFromParsingName(start_dir.c_str(), nullptr, IID_PPV_ARGS(&start)))) {
        dlg->SetFolder(start);
        start->Release();
    }
    if (SUCCEEDED(dlg->Show(owner))) {
        IShellItem* item = nullptr;
        if (SUCCEEDED(dlg->GetResult(&item))) {
            PWSTR name = nullptr;
            if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &name))) {
                out = name;
                CoTaskMemFree(name);
                chosen = true;
            }
            item->Release();
        }
    }
    dlg->Release();
    return chosen;
}

std::vector<Entry> list() { return list(path()); }

std::vector<Entry> list(const std::wstring& dir) {
    std::vector<Entry> out;
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

void open_in_explorer() { open_in_explorer(path()); }

void open_in_explorer(const std::wstring& dir) {
    ShellExecuteW(nullptr, L"open", dir.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

} // namespace wsz::skins_folder
