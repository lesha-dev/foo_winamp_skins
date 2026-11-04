#include "stdafx.h"
#include "skin.h"

#include "config.h"

#include <miniz.h>

#include <cctype>

namespace wsz {
namespace {

constexpr size_t max_file_size = 16u * 1024 * 1024;

const char* const bitmap_files[] = {
    "main.bmp", "titlebar.bmp", "cbuttons.bmp", "shufrep.bmp", "posbar.bmp", "volume.bmp", "balance.bmp",
    "monoster.bmp", "playpaus.bmp", "numbers.bmp", "nums_ex.bmp", "text.bmp", "eqmain.bmp", "eq_ex.bmp",
    "pledit.bmp", "gen.bmp", "genex.bmp", "video.bmp",
};
static_assert(std::size(bitmap_files) == static_cast<size_t>(SkinBitmap::count_));

const char* const cursor_files[] = {
    "volbal.cur", "posbar.cur", "winbut.cur", "min.cur", "close.cur", "mainmenu.cur", "titlebar.cur", "songname.cur", "normal.cur",
    "winbut.cur", "min.cur", "wsposbar.cur", "close.cur", "mmenu.cur", "wsnormal.cur",
    "pwinbut.cur", "pclose.cur", "ptbar.cur", "pvscroll.cur", "psize.cur", "pnormal.cur",
    "pwinbut.cur", "pclose.cur", "pwssize.cur", "pwsnorm.cur",
    "eqslid.cur", "eqclose.cur", "eqtitle.cur", "eqnormal.cur",
};
static_assert(std::size(cursor_files) == static_cast<size_t>(SkinCursor::count_));

// LoadImage handles both static (.cur) and animated (RIFF .ani) cursors, but only from a file.
HCURSOR load_cursor(const std::vector<uint8_t>& data) {
    wchar_t dir[MAX_PATH], path[MAX_PATH];
    if (!GetTempPathW(MAX_PATH, dir) || !GetTempFileNameW(dir, L"wsz", 0, path)) return nullptr;
    HCURSOR c = nullptr;
    HANDLE f = CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_TEMPORARY, nullptr);
    if (f != INVALID_HANDLE_VALUE) {
        DWORD written = 0;
        const bool ok = WriteFile(f, data.data(), static_cast<DWORD>(data.size()), &written, nullptr) && written == data.size();
        CloseHandle(f);
        if (ok) c = static_cast<HCURSOR>(LoadImageW(nullptr, path, IMAGE_CURSOR, 0, 0, LR_LOADFROMFILE));
    }
    DeleteFileW(path);
    return c;
}

// Winamp's built-in visualization palette, used when viscolor.txt is missing or short.
constexpr std::array<COLORREF, 24> default_vis_colors = {
    RGB(0, 0, 0),       RGB(24, 33, 41),    RGB(239, 49, 16),   RGB(206, 41, 16),
    RGB(214, 90, 0),    RGB(214, 102, 0),   RGB(214, 115, 0),   RGB(198, 123, 8),
    RGB(222, 165, 24),  RGB(214, 181, 33),  RGB(189, 222, 41),  RGB(148, 222, 33),
    RGB(41, 206, 16),   RGB(50, 190, 16),   RGB(57, 181, 16),   RGB(49, 156, 8),
    RGB(41, 148, 0),    RGB(24, 132, 8),    RGB(255, 255, 255), RGB(214, 214, 222),
    RGB(181, 189, 189), RGB(160, 170, 175), RGB(148, 156, 165), RGB(150, 150, 150),
};

std::string to_lower(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string trim(const std::string& s) {
    size_t b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return {};
    size_t e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

std::vector<std::string> split_lines(const std::vector<uint8_t>& data) {
    std::vector<std::string> lines;
    std::string cur;
    for (uint8_t c : data) {
        if (c == '\n' || c == '\r') { lines.push_back(std::move(cur)); cur.clear(); }
        else if (c != 0) cur.push_back(static_cast<char>(c));
    }
    lines.push_back(std::move(cur));
    return lines;
}

std::vector<int> parse_ints(const std::string& s) {
    std::vector<int> out;
    size_t i = 0;
    while (i < s.size()) {
        bool neg = false;
        if (s[i] == '-' && i + 1 < s.size() && std::isdigit(static_cast<unsigned char>(s[i + 1]))) { neg = true; ++i; }
        if (std::isdigit(static_cast<unsigned char>(s[i]))) {
            long v = 0;
            while (i < s.size() && std::isdigit(static_cast<unsigned char>(s[i]))) {
                v = std::min(v * 10 + (s[i] - '0'), 1000000L);
                ++i;
            }
            out.push_back(static_cast<int>(neg ? -v : v));
        } else {
            ++i;
        }
    }
    return out;
}

using IniSection = std::map<std::string, std::string>;
using Ini = std::map<std::string, IniSection>;

// Lenient INI: case-insensitive sections/keys, ';' and '//' comments, duplicate keys -> first wins.
Ini parse_ini(const std::vector<uint8_t>& data) {
    Ini ini;
    std::string section;
    for (auto& raw : split_lines(data)) {
        std::string line = trim(raw);
        if (line.empty() || line[0] == ';' || line.rfind("//", 0) == 0) continue;
        if (line[0] == '[') {
            size_t e = line.find(']');
            section = to_lower(trim(line.substr(1, e == std::string::npos ? std::string::npos : e - 1)));
            continue;
        }
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = to_lower(trim(line.substr(0, eq)));
        ini[section].emplace(key, trim(line.substr(eq + 1)));
    }
    return ini;
}

std::optional<COLORREF> parse_hex_color(std::string v) {
    v = trim(v);
    if (!v.empty() && v[0] == '#') v.erase(0, 1);
    if (v.size() < 6) return std::nullopt;
    unsigned rgb = 0;
    for (size_t i = 0; i < 6; ++i) {
        int c = std::tolower(static_cast<unsigned char>(v[i]));
        int d = std::isdigit(c) ? c - '0' : (c >= 'a' && c <= 'f') ? c - 'a' + 10 : -1;
        if (d < 0) return std::nullopt;
        rgb = (rgb << 4) | static_cast<unsigned>(d);
    }
    return RGB((rgb >> 16) & 0xff, (rgb >> 8) & 0xff, rgb & 0xff);
}

std::vector<uint8_t> read_file(const std::wstring& path) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) throw std::runtime_error(pfc::format("cannot open file (error ", (uint32_t)GetLastError(), ")").c_str());
    std::vector<uint8_t> data;
    LARGE_INTEGER size{};
    bool ok = GetFileSizeEx(h, &size) && size.QuadPart >= 0 && static_cast<uint64_t>(size.QuadPart) <= max_file_size * 4;
    if (ok) {
        data.resize(static_cast<size_t>(size.QuadPart));
        DWORD read = 0;
        ok = data.empty() || (ReadFile(h, data.data(), static_cast<DWORD>(data.size()), &read, nullptr) && read == data.size());
    }
    CloseHandle(h);
    if (!ok) throw std::runtime_error("cannot read file");
    return data;
}

std::string basename_lower(const char* name) {
    std::string n = name;
    size_t slash = n.find_last_of("/\\");
    return to_lower(slash == std::string::npos ? n : n.substr(slash + 1));
}

void read_zip(const std::vector<uint8_t>& archive, std::map<std::string, std::vector<uint8_t>>& files) {
    mz_zip_archive zip{};
    if (!mz_zip_reader_init_mem(&zip, archive.data(), archive.size(), 0))
        throw std::runtime_error(pfc::format("not a valid zip archive (", mz_zip_get_error_string(mz_zip_get_last_error(&zip)), ")").c_str());

    const mz_uint count = mz_zip_reader_get_num_files(&zip);
    for (mz_uint i = 0; i < count; ++i) {
        mz_zip_archive_file_stat st;
        if (!mz_zip_reader_file_stat(&zip, i, &st) || st.m_is_directory || !st.m_is_supported) continue;
        if (st.m_uncomp_size > max_file_size) continue;
        std::string full = to_lower(st.m_filename);
        if (full.find("__macosx") != std::string::npos) continue;
        std::string name = basename_lower(st.m_filename);
        if (name.empty() || files.count(name)) continue; // first occurrence wins

        size_t size = 0;
        void* p = mz_zip_reader_extract_to_heap(&zip, i, &size, 0);
        if (!p) continue;
        files[name].assign(static_cast<uint8_t*>(p), static_cast<uint8_t*>(p) + size);
        mz_free(p);
    }
    mz_zip_reader_end(&zip);
}

void read_directory(const std::wstring& dir, std::map<std::string, std::vector<uint8_t>>& files) {
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((dir + L"\\*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) throw std::runtime_error("cannot list skin directory");
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        if (fd.nFileSizeHigh || fd.nFileSizeLow > max_file_size) continue;
        std::string name = to_lower(pfc::stringcvt::string_utf8_from_wide(fd.cFileName).get_ptr());
        try { files[name] = read_file(dir + L"\\" + fd.cFileName); } catch (...) {}
    } while (FindNextFileW(h, &fd));
    FindClose(h);
}

// Skin config files that are not the author's notes.
bool is_config_txt(const std::string& name) {
    static const char* const known[] = { "viscolor.txt", "pledit.txt", "region.txt", "winampmb.txt" };
    for (auto* k : known) if (name == k) return true;
    return false;
}

bool is_text_ext(const std::string& name) {
    for (auto* ext : { ".txt", ".nfo", ".diz" })
        if (name.size() > 4 && name.compare(name.size() - 4, 4, ext) == 0) return true;
    return false;
}

// Readmes are mostly ANSI from the Winamp 2 era; accept UTF-8 and UTF-16 (BOM) too.
std::string decode_text(const std::vector<uint8_t>& data) {
    constexpr size_t max_len = 64 * 1024;
    const size_t n = std::min(data.size(), max_len);
    if (n >= 2 && data[0] == 0xFF && data[1] == 0xFE) {
        std::wstring w(reinterpret_cast<const wchar_t*>(data.data() + 2), (n - 2) / 2);
        return pfc::stringcvt::string_utf8_from_wide(w.c_str()).get_ptr();
    }
    std::string s(reinterpret_cast<const char*>(data.data()), n);
    if (s.rfind("\xEF\xBB\xBF", 0) == 0) s.erase(0, 3);
    s.erase(std::remove(s.begin(), s.end(), '\0'), s.end());
    if (pfc::is_valid_utf8(s.c_str())) return s;
    return pfc::stringcvt::string_utf8_from_ansi(s.c_str()).get_ptr();
}

} // namespace

Skin::Skin() : m_vis_colors(default_vis_colors) {}

void set_skin_cursor(const Skin* skin, SkinCursor c) {
    using C = SkinCursor;
    // Cursors of the same kind (Winamp's built-in cursor groups), tried in order when one is missing.
    static const std::initializer_list<C> lr_scroll = { C::volbal, C::posbar, C::songname, C::ws_posbar, C::plws_size };
    static const std::initializer_list<C> ud_scroll = { C::pl_vscroll, C::eq_slider };
    static const std::initializer_list<C> danger = { C::close, C::ws_close, C::pl_close, C::plws_close, C::eq_close };
    static const std::initializer_list<C> move = { C::titlebar, C::pl_titlebar, C::eq_titlebar };
    HCURSOR h = nullptr;
    if (skin && config::skin_cursors.get()) {
        h = skin->cursor(c);
        for (const auto* group : { &lr_scroll, &ud_scroll, &danger, &move }) {
            if (h || std::find(group->begin(), group->end(), c) == group->end()) continue;
            for (C other : *group) if ((h = skin->cursor(other)) != nullptr) break;
        }
        // Winamp fills the rest with its built-in cursors; we use the skin's Normal.cur instead.
        if (!h) h = skin->cursor(C::normal);
    }
    SetCursor(h ? h : LoadCursorW(nullptr, IDC_ARROW));
}

Skin::~Skin() {
    for (HCURSOR c : m_cursors) if (c) DestroyCursor(c);
}

std::unique_ptr<Skin> Skin::load(const char* path_utf8) {
    std::wstring wpath = pfc::stringcvt::string_wide_from_utf8(path_utf8).get_ptr();
    const DWORD attr = GetFileAttributesW(wpath.c_str());
    if (attr == INVALID_FILE_ATTRIBUTES) throw std::runtime_error("skin not found");

    FileMap files;
    if (attr & FILE_ATTRIBUTE_DIRECTORY) {
        while (!wpath.empty() && (wpath.back() == L'\\' || wpath.back() == L'/')) wpath.pop_back();
        read_directory(wpath, files);
    } else {
        read_zip(read_file(wpath), files);
    }

    std::unique_ptr<Skin> skin(new Skin());
    skin->m_path = path_utf8;
    skin->load_from(files);
    if (skin->bitmap(SkinBitmap::main).empty()) throw std::runtime_error("not a Winamp 2 skin (main.bmp missing or unreadable)");
    return skin;
}

void Skin::load_from(FileMap& files) {
    for (size_t i = 0; i < std::size(bitmap_files); ++i) {
        auto it = files.find(bitmap_files[i]);
        if (it == files.end()) continue;
        try {
            m_bitmaps[i] = decode_image(it->second.data(), it->second.size());
        } catch (const std::exception& e) {
            m_warnings.push_back(pfc::format(bitmap_files[i], ": ", e.what()).c_str());
        }
    }

    for (size_t i = 0; i < std::size(cursor_files); ++i) {
        if (auto it = files.find(cursor_files[i]); it != files.end() && !it->second.empty()) m_cursors[i] = load_cursor(it->second);
    }

    if (auto it = files.find("viscolor.txt"); it != files.end()) {
        size_t n = 0;
        for (auto& line : split_lines(it->second)) {
            if (n >= m_vis_colors.size()) break;
            std::string l = line.substr(0, line.find("//"));
            auto v = parse_ints(l);
            if (v.size() < 3) continue;
            auto c = [](int x) { return static_cast<BYTE>(std::clamp(x, 0, 255)); };
            m_vis_colors[n++] = RGB(c(v[0]), c(v[1]), c(v[2]));
        }
    }

    if (auto it = files.find("pledit.txt"); it != files.end()) {
        Ini ini = parse_ini(it->second);
        const IniSection& t = ini["text"];
        auto color = [&](const char* key, COLORREF& out) {
            if (auto k = t.find(key); k != t.end())
                if (auto c = parse_hex_color(k->second)) out = *c;
        };
        color("normal", m_pl_colors.normal);
        color("current", m_pl_colors.current);
        color("normalbg", m_pl_colors.normal_bg);
        color("selectedbg", m_pl_colors.selected_bg);
        if (auto k = t.find("font"); k != t.end() && !k->second.empty()) m_pl_colors.font = k->second;
    }

    // Readme: prefer a file called readme*, otherwise the first other text file.
    const std::vector<uint8_t>* readme = nullptr;
    for (auto& [name, data] : files) {
        if (!is_text_ext(name) || is_config_txt(name)) continue;
        const bool named = name.find("readme") != std::string::npos || name.find("read me") != std::string::npos;
        if (named || !readme) readme = &data;
        if (named) break;
    }
    if (readme) m_readme = decode_text(*readme);

    if (auto it = files.find("region.txt"); it != files.end()) {
        for (auto& [section, keys] : parse_ini(it->second)) {
            auto np = keys.find("numpoints");
            auto pl = keys.find("pointlist");
            if (np == keys.end() || pl == keys.end()) continue;
            const auto counts = parse_ints(np->second);
            const auto coords = parse_ints(pl->second);
            std::vector<RegionPolygon> polys;
            size_t pos = 0;
            for (int count : counts) {
                if (count < 3 || pos + size_t(count) * 2 > coords.size()) break;
                RegionPolygon poly;
                for (int i = 0; i < count; ++i, pos += 2) poly.points.push_back({ coords[pos], coords[pos + 1] });
                polys.push_back(std::move(poly));
            }
            if (!polys.empty()) m_regions[section] = std::move(polys);
        }
    }
}

} // namespace wsz
