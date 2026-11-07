#include "stdafx.h"
#include "eq.h"
#include "config.h"
#include "guids.h"
#include "main_window.h"

#include <atomic>
#include <cmath>
#include <numbers>

namespace wsz::eq {
namespace {

std::array<std::atomic<int>, band_count + 1> g_values; // [0] = preamp
std::atomic<bool> g_on{ false };
std::once_flag g_loaded;

void load() {
    std::call_once(g_loaded, [] {
        Values v;
        const pfc::string8 s = config::eq_values.get();
        int idx = 0;
        const char* p = s.get_ptr();
        while (*p && idx <= band_count) {
            const int n = std::clamp(atoi(p), 0, 100);
            if (idx == 0) v.preamp = n; else v.bands[idx - 1] = n;
            ++idx;
            while (*p && *p != ',') ++p;
            if (*p == ',') ++p;
        }
        g_values[0] = v.preamp;
        for (int i = 0; i < band_count; ++i) g_values[i + 1] = v.bands[i];
        g_on = config::eq_on.get();
    });
}

// foobar2000's built-in Equalizer (foo_dsp_eq): 18 bands, preset data = one byte per band, 20 - dB (0..40).
constexpr GUID builtin_guid = { 0x82aef845, 0xdcc3, 0x4da5, { 0x9d, 0x80, 0xe9, 0xa9, 0x72, 0xb2, 0x14, 0x0d } };
constexpr int builtin_count = 18;
constexpr std::array<double, builtin_count> builtin_hz = {
    55, 77, 110, 156, 220, 311, 440, 622, 880, 1200, 1800, 2500, 3500, 5000, 7000, 10000, 14000, 20000 };
constexpr double builtin_max_db = 20.0;
using Data = std::array<uint8_t, builtin_count>;

std::array<Data, 16> g_recent{};  // recent pushes: chain notifications may arrive late, after newer pushes
size_t g_recent_count = 0, g_recent_next = 0;

void remember(const Data& d) {
    g_recent[g_recent_next] = d;
    g_recent_next = (g_recent_next + 1) % g_recent.size();
    g_recent_count = std::min(g_recent_count + 1, g_recent.size());
}

bool ours(const Data& d) {
    for (size_t i = 0; i < g_recent_count; ++i) if (g_recent[i] == d) return true;
    return false;
}
service_ptr g_live_edit;
bool g_editing = false;

// Piecewise-linear in log-frequency, flat beyond the end points.
double interpolate(const double* hz, const double* db, int n, double f) {
    if (f <= hz[0]) return db[0];
    if (f >= hz[n - 1]) return db[n - 1];
    int i = 1;
    while (hz[i] < f) ++i;
    const double t = std::log(f / hz[i - 1]) / std::log(hz[i] / hz[i - 1]);
    return db[i - 1] + (db[i] - db[i - 1]) * t;
}

Data to_builtin(const Values& v) {
    double db[band_count];
    for (int i = 0; i < band_count; ++i) db[i] = to_db(v.bands[i]);
    const double pre = to_db(v.preamp);
    Data d{};
    for (int i = 0; i < builtin_count; ++i) {
        const double x = std::clamp(interpolate(band_hz.data(), db, band_count, builtin_hz[i]) + pre, -builtin_max_db, builtin_max_db);
        d[i] = static_cast<uint8_t>(builtin_max_db - std::lround(x));
    }
    return d;
}

Values from_builtin(const Data& d) {
    double db[builtin_count];
    for (int i = 0; i < builtin_count; ++i) db[i] = builtin_max_db - std::min<int>(d[i], 40);
    Values v;
    for (int i = 0; i < band_count; ++i) {
        const double x = interpolate(builtin_hz.data(), db, builtin_count, band_hz[i]);
        v.bands[i] = std::clamp(static_cast<int>(std::lround(50 + x / max_db * 50)), 0, 100);
    }
    return v;
}

bool read_data(const dsp_preset& p, Data& out) {
    if (p.get_data_size() != builtin_count) return false;
    memcpy(out.data(), p.get_data(), builtin_count);
    return true;
}

void store(const Values& v) {
    g_values[0] = std::clamp(v.preamp, 0, 100);
    pfc::string8 s = pfc::format(g_values[0].load());
    for (int i = 0; i < band_count; ++i) {
        g_values[i + 1] = std::clamp(v.bands[i], 0, 100);
        s << "," << g_values[i + 1].load();
    }
    config::eq_values = s;
}

void notify() {
    if (auto* mw = MainWindow::instance()) mw->on_eq_changed();
}

// Writes our values into the built-in EQ's chain entry, inserting it if missing.
void push() {
    if (!core_api::is_main_thread()) return;
    const Data d = to_builtin(get());
    auto mgr = dsp_config_manager::get();
    dsp_preset_impl cur;
    const bool present = mgr->core_query_dsp(builtin_guid, cur);
    Data old{};
    remember(d);
    if (present && read_data(cur, old) && old == d) return;
    dsp_preset_impl preset;
    preset.set_owner(builtin_guid);
    preset.set_data(d.data(), d.size());
    mgr->core_enable_dsp(preset, dsp_config_manager::default_insert_last);
}

// Writes exact band data (e.g. from an .feq file) into the built-in EQ's chain entry.
void write_builtin(const Data& d) {
    remember(d);
    dsp_preset_impl preset;
    preset.set_owner(builtin_guid);
    preset.set_data(d.data(), d.size());
    dsp_config_manager::get()->core_enable_dsp(preset, dsp_config_manager::default_insert_last);
}

// Rebuilding the DSP chain is slow, so slider drags push at most every push_interval ms.
constexpr UINT push_interval = 60;
UINT_PTR g_push_timer = 0;

void CALLBACK on_push_timer(HWND, UINT, UINT_PTR, DWORD) {
    KillTimer(nullptr, g_push_timer);
    g_push_timer = 0;
    if (g_on) push();
}

void schedule_push() {
    if (g_push_timer == 0) g_push_timer = SetTimer(nullptr, 0, push_interval, on_push_timer);
}

void flush_push() {
    if (g_push_timer == 0) return;
    KillTimer(nullptr, g_push_timer);
    g_push_timer = 0;
    if (g_on) push();
}

// Keeps the window in sync when the built-in EQ is edited, added or removed elsewhere.
class DspCallback : public dsp_config_callback {
public:
    void on_core_settings_change(const dsp_chain_config& chain) override {
        load();
        for (size_t i = 0; i < chain.get_count(); ++i) {
            const dsp_preset& p = chain.get_item(i);
            if (p.get_owner() != builtin_guid) continue;
            Data d{};
            if (g_editing || !read_data(p, d) || ours(d)) break;
            g_recent_count = 0;
            remember(d);
            store(from_builtin(d));
            break;
        }
        // Notifications can lag behind rapid on/off toggles: take presence from the current chain.
        dsp_preset_impl cur;
        const bool present = dsp_config_manager::get()->core_query_dsp(builtin_guid, cur);
        if (present != g_on.load()) {
            g_on = present;
            config::eq_on = present;
        }
        notify();
    }
};

FB2K_SERVICE_FACTORY(DspCallback);

} // namespace

Values get() {
    load();
    Values v;
    v.preamp = g_values[0];
    for (int i = 0; i < band_count; ++i) v.bands[i] = g_values[i + 1];
    return v;
}

void set(const Values& v) {
    load();
    store(v);
    if (!g_on) return;
    if (g_editing) schedule_push();
    else { flush_push(); push(); }
}

bool enabled() { load(); return g_on; }

void set_enabled(bool on) {
    load();
    g_on = on;
    config::eq_on = on;
    if (on) push();
    else dsp_config_manager::get()->core_disable_dsp(builtin_guid);
}

void sync() {
    load();
    dsp_preset_impl cur;
    const bool present = dsp_config_manager::get()->core_query_dsp(builtin_guid, cur);
    if (present) {
        Data d{};
        if (read_data(cur, d) && d != to_builtin(get())) store(from_builtin(d));
    }
    g_on = present;
    config::eq_on = present;
}

void begin_edit() {
    if (g_editing) return;
    g_editing = true;
    auto mgr = dsp_config_manager::get();
    dsp_config_manager_v3::ptr v3;
    if (v3 &= mgr.get_ptr()) g_live_edit = v3->acquire_live_edit_scope();
}

void end_edit() {
    g_editing = false;
    flush_push();
    g_live_edit.release();
}

bool load_feq(const char* path_utf8) {
    FILE* f = nullptr;
    if (_wfopen_s(&f, pfc::stringcvt::string_wide_from_utf8(path_utf8), L"rb") != 0 || !f) return false;
    char buf[1024];
    const size_t len = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    buf[len] = 0;
    Data d{};
    int n = 0;
    for (char* p = buf; n < builtin_count;) {
        char* end = nullptr;
        const long x = strtol(p, &end, 10);
        if (end == p) break;
        d[n++] = static_cast<uint8_t>(builtin_max_db - std::clamp<long>(x, -20, 20));
        p = end;
    }
    if (n != builtin_count) return false;
    // The 18 bands go into fb2k's Equalizer as they are; our sliders follow from them.
    load();
    flush_push();
    store(from_builtin(d));
    if (g_on) write_builtin(d);
    notify();
    return true;
}

bool save_feq(const char* path_utf8) {
    FILE* f = nullptr;
    if (_wfopen_s(&f, pfc::stringcvt::string_wide_from_utf8(path_utf8), L"wb") != 0 || !f) return false;
    // Save what fb2k's Equalizer actually has, so files round-trip exactly.
    flush_push();
    Data d = to_builtin(get());
    dsp_preset_impl cur;
    if (dsp_config_manager::get()->core_query_dsp(builtin_guid, cur)) read_data(cur, d);
    for (uint8_t b : d) fprintf(f, "%d\r\n", static_cast<int>(builtin_max_db) - b);
    return fclose(f) == 0;
}

} // namespace wsz::eq
