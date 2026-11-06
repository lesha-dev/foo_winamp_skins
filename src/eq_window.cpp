#include "stdafx.h"
#include "eq_window.h"
#include "balance_dsp.h"
#include "config.h"
#include "main_window.h"
#include "player.h"

#include <cmath>

namespace wsz {
namespace {

using S = layout::Sprite;
using P = layout::Pos;

// eqmain.bmp
constexpr S bg            { 0, 0, 275, 116 };
constexpr S title_active  { 0, 134, 275, 14 };
constexpr S title_inactive{ 0, 149, 275, 14 };
constexpr S thumb         { 0, 164, 11, 11 };
constexpr S thumb_down    { 0, 176, 11, 11 };
constexpr S on_off        { 10, 119, 26, 12 };
constexpr S on_off_down   { 128, 119, 26, 12 };
constexpr S on_on         { 69, 119, 26, 12 };
constexpr S on_on_down    { 187, 119, 26, 12 };
constexpr S auto_off      { 36, 119, 32, 12 };
constexpr S auto_off_down { 154, 119, 32, 12 };
constexpr S auto_on       { 95, 119, 32, 12 };
constexpr S auto_on_down  { 213, 119, 32, 12 };
constexpr S graph_bg      { 0, 294, 113, 19 };
constexpr S graph_colors  { 115, 294, 1, 19 };
constexpr S preamp_line   { 0, 314, 113, 1 };
constexpr S presets_btn   { 224, 164, 44, 12 };
constexpr S presets_down  { 224, 176, 44, 12 };
constexpr S close_down    { 0, 116, 9, 9 };
constexpr int slider_frame_x = 13, slider_frame_y = 164, slider_w = 14, slider_h = 63;
constexpr int slider_travel = 51; // thumb top offset range within the 63px slider

constexpr P pos_on { 14, 18 }, pos_auto { 40, 18 }, pos_presets { 217, 18 }, pos_graph { 86, 17 };
constexpr P pos_preamp { 21, 38 };
constexpr int band_x0 = 78, band_dx = 18, band_y = 38;
constexpr P pos_shade_btn { 254, 3 }, pos_close { 264, 3 };

// eq_ex.bmp (windowshade)
constexpr S shade_active   { 0, 0, 275, 14 };
constexpr S shade_inactive { 0, 15, 275, 14 };
constexpr S shade_vol_thumb[3] = { { 1, 30, 3, 7 }, { 4, 30, 3, 7 }, { 7, 30, 3, 7 } };
constexpr S shade_bal_thumb[3] = { { 11, 30, 3, 7 }, { 14, 30, 3, 7 }, { 17, 30, 3, 7 } };
constexpr S shade_unshade_down { 1, 38, 9, 9 };
constexpr S shade_btn_down     { 1, 47, 9, 9 }; // normal mode shade button, pressed
constexpr S shade_close        { 11, 38, 9, 9 };
constexpr S shade_close_down   { 11, 47, 9, 9 };
constexpr P pos_shade_vol { 61, 4 }, pos_shade_bal { 164, 4 };
constexpr int shade_vol_w = 97, shade_bal_w = 43;

struct R { int x, y, w, h; bool in(POINT p) const { return p.x >= x && p.x < x + w && p.y >= y && p.y < y + h; } };

int slider_x(int i) { return i < 0 ? pos_preamp.x : band_x0 + i * band_dx; }

// Natural cubic spline through the 10 band points (Webamp's EQ graph), evaluated per pixel.
std::vector<double> spline(const std::vector<double>& xs, const std::vector<double>& ys) {
    const size_t n = xs.size();
    std::vector<double> a(ys), b(n), d(n), h(n - 1), alpha(n), c(n), l(n), mu(n), z(n);
    for (size_t i = 0; i + 1 < n; ++i) h[i] = xs[i + 1] - xs[i];
    for (size_t i = 1; i + 1 < n; ++i) alpha[i] = 3 / h[i] * (a[i + 1] - a[i]) - 3 / h[i - 1] * (a[i] - a[i - 1]);
    l[0] = 1;
    for (size_t i = 1; i + 1 < n; ++i) {
        l[i] = 2 * (xs[i + 1] - xs[i - 1]) - h[i - 1] * mu[i - 1];
        mu[i] = h[i] / l[i];
        z[i] = (alpha[i] - h[i - 1] * z[i - 1]) / l[i];
    }
    l[n - 1] = 1;
    for (size_t j = n - 1; j-- > 0;) {
        c[j] = z[j] - mu[j] * c[j + 1];
        b[j] = (a[j + 1] - a[j]) / h[j] - h[j] * (c[j + 1] + 2 * c[j]) / 3;
        d[j] = (c[j + 1] - c[j]) / (3 * h[j]);
    }
    std::vector<double> out;
    for (int x = 0; x <= static_cast<int>(xs.back()); ++x) {
        size_t j = std::min(n - 2, static_cast<size_t>(x / h[0]));
        while (j > 0 && x < xs[j]) --j;
        const double t = x - xs[j];
        out.push_back(a[j] + b[j] * t + c[j] * t * t + d[j] * t * t * t);
    }
    return out;
}

} // namespace

void EqWindow::set_shade(bool on) {
    m_shade = on;
    config::eq_shade = on;
    m_pressed = Part::none;
    apply_geometry();
}

void EqWindow::on_double_size_changed() { config::eq_double = is_double_size(); }

void EqWindow::save_position() {
    RECT rc;
    if (m_wnd && GetWindowRect(m_wnd, &rc)) on_position_saved(rc.left, rc.top);
}

void EqWindow::on_position_saved(int x, int y) {
    config::eq_x = x;
    config::eq_y = y;
}

void EqWindow::on_visibility_changed(bool visible) {
    config::eq_visible = visible;
    if (MainWindow* w = MainWindow::instance()) w->invalidate();
}

// Hit areas follow Winamp's equalizer cursor map.
SkinCursor EqWindow::cursor_at(POINT p) const {
    if (!m_shade) {
        for (int i = 0; i < 11; ++i) {
            const int x = i == 0 ? 21 : 78 + (i - 1) * 18;
            if (in_rect(p, x, 39, x + 12, 98)) return SkinCursor::eq_slider;
        }
    }
    if (in_rect(p, 264, 3, 272, 12)) return SkinCursor::eq_close;
    if (in_rect(p, 0, 0, 275, 13)) return SkinCursor::eq_titlebar;
    return SkinCursor::eq_normal;
}

const wchar_t* EqWindow::tooltip_at(POINT p) const {
    static const wchar_t* const bands[eq::band_count] = { L"70Hz", L"180Hz", L"320Hz", L"600Hz", L"1KHz", L"3KHz", L"6KHz", L"12KHz", L"14KHz", L"16KHz" };
    const Part part = hit(p);
    switch (part) {
    case Part::close: return L"Close";
    case Part::shade: return L"Toggle Windowshade Mode";
    case Part::on: return L"EQ on/off";
    case Part::autob: return L"EQ auto-load by track (doesn't work)";
    case Part::presets: return L"Open presets";
    case Part::preamp: return L"Preamp";
    case Part::volume: return L"Volume";
    case Part::balance: return L"Balance";
    default: break;
    }
    const int band = static_cast<int>(part) - static_cast<int>(Part::band0);
    return band >= 0 && band < eq::band_count ? bands[band] : nullptr;
}

EqWindow::Part EqWindow::hit(POINT p) const {
    if (R{ pos_close.x, pos_close.y, 9, 9 }.in(p)) return Part::close;
    if (R{ pos_shade_btn.x, pos_shade_btn.y, 9, 9 }.in(p)) return Part::shade;
    if (m_shade) {
        if (R{ pos_shade_vol.x, pos_shade_vol.y - 1, shade_vol_w, 8 }.in(p)) return Part::volume;
        if (R{ pos_shade_bal.x, pos_shade_bal.y - 1, shade_bal_w, 8 }.in(p)) return Part::balance;
        return Part::none;
    }
    if (R{ pos_on.x, pos_on.y, on_off.w, on_off.h }.in(p)) return Part::on;
    if (R{ pos_auto.x, pos_auto.y, auto_off.w, auto_off.h }.in(p)) return Part::autob;
    if (R{ pos_presets.x, pos_presets.y, presets_btn.w, presets_btn.h }.in(p)) return Part::presets;
    if (R{ pos_preamp.x, band_y, slider_w, slider_h }.in(p)) return Part::preamp;
    for (int i = 0; i < eq::band_count; ++i)
        if (R{ slider_x(i), band_y, slider_w, slider_h }.in(p)) return static_cast<Part>(static_cast<int>(Part::band0) + i);
    return Part::none;
}

bool EqWindow::is_control(POINT p) const { return hit(p) != Part::none; }

void EqWindow::on_mouse_down(POINT p, bool) {
    m_pressed = hit(p);
    m_inside = true;
    if (m_pressed >= Part::preamp) drag_to(p);
    invalidate();
}

void EqWindow::on_mouse_move(POINT p) {
    if (m_pressed == Part::none) return;
    if (m_pressed >= Part::preamp) drag_to(p);
    else m_inside = hit(p) == m_pressed;
    invalidate();
}

void EqWindow::on_mouse_up(POINT p) {
    eq::end_edit();
    if (m_pressed >= Part::preamp) if (auto* mw = MainWindow::instance()) mw->set_status(nullptr);
    const Part part = m_pressed;
    const bool fire = part != Part::none && part < Part::preamp && hit(p) == part;
    m_pressed = Part::none;
    invalidate();
    if (!fire) return;
    switch (part) {
    case Part::close: show(false); break;
    case Part::shade: set_shade(!m_shade); break;
    case Part::on: eq::set_enabled(!eq::enabled()); break;
    case Part::autob: config::eq_auto = !config::eq_auto.get(); break;
    case Part::presets: show_presets_menu(); break;
    default: break;
    }
    invalidate();
}

void EqWindow::on_mouse_leave_capture() {
    eq::end_edit();
    if (m_pressed >= Part::preamp) if (auto* mw = MainWindow::instance()) mw->set_status(nullptr);
    m_pressed = Part::none;
    invalidate();
}

namespace {
void status(const char* text) {
    if (auto* mw = MainWindow::instance()) mw->set_status(text);
}
} // namespace

void EqWindow::drag_to(POINT p) {
    if (m_pressed == Part::volume) {
        const double t = std::clamp((p.x - pos_shade_vol.x - 1) / double(shade_vol_w - 3), 0.0, 1.0);
        playback_control::get()->set_volume(static_cast<float>(player::slider_to_db(t)));
        status(pfc::format("Volume: ", static_cast<int>(t * 100 + 0.5), "%"));
        return;
    }
    if (m_pressed == Part::balance) {
        if (!balance::enabled()) return;
        double b = std::clamp((p.x - pos_shade_bal.x - 1) / double(shade_bal_w - 3), 0.0, 1.0) * 2 - 1;
        if (std::abs(b) * 100 < static_cast<double>(config::balance_snap.get())) b = 0;
        balance::set(b);
        if (b != 0) balance::ensure_dsp();
        const int pct = static_cast<int>(std::abs(b) * 100 + 0.5);
        status(pct == 0 ? pfc::string8("Balance: Center") : pfc::format("Balance: ", pct, "% ", b < 0 ? "Left" : "Right"));
        return;
    }
    // Vertical EQ slider: thumb centre follows the cursor.
    // 52 thumb positions; the two middle ones both mean 0 dB so the centre is reachable.
    const int off = std::clamp(static_cast<int>(p.y) - band_y - thumb.h / 2, 0, slider_travel);
    constexpr int half = slider_travel / 2; // 25
    const int v = off <= half ? 100 - (off * 50 + half / 2) / half
                              : 50 - ((off - half - 1) * 50 + half / 2) / half;
    eq::begin_edit();
    eq::Values vals = eq::get();
    const auto db = [](int x) { return pfc::format(x >= 50 ? "+" : "-", pfc::format_float(std::abs(eq::to_db(x)), 0, 1), " dB"); };
    if (m_pressed == Part::preamp) {
        vals.preamp = v;
        status(pfc::format("EQ: Preamp: ", db(v)));
    } else {
        const int band = static_cast<int>(m_pressed) - static_cast<int>(Part::band0);
        vals.bands[band] = v;
        const int hz = static_cast<int>(eq::band_hz[band]);
        status(pfc::format("EQ: ", hz >= 1000 ? pfc::format(hz / 1000, "KHz") : pfc::format(hz, "Hz"), ": ", db(v)));
    }
    eq::set(vals);
}

void EqWindow::on_wheel(int delta) {
    if (delta > 0) playback_control::get()->volume_up();
    else playback_control::get()->volume_down();
}

void EqWindow::show_presets_menu() {
    enum : UINT { id_reset = 1, id_load_feq, id_save_feq, id_preset_first = 0x100 };
    HMENU load = CreatePopupMenu();
    const auto& list = eq::presets();
    for (size_t i = 0; i < list.size(); ++i)
        AppendMenuW(load, MF_STRING, id_preset_first + i, pfc::stringcvt::string_wide_from_utf8(list[i].name.c_str()));
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_POPUP | MF_STRING, reinterpret_cast<UINT_PTR>(load), L"&Load");
    AppendMenuW(menu, MF_STRING, id_load_feq, L"Load from &FEQ file...");
    AppendMenuW(menu, MF_STRING, id_save_feq, L"&Save to FEQ file...");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, id_reset, L"&Reset");
    POINT pt = { (pos_presets.x) * m_scale, (pos_presets.y + presets_btn.h) * m_scale };
    ClientToScreen(m_wnd, &pt);
    const UINT cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_NONOTIFY, pt.x, pt.y, 0, m_wnd, nullptr);
    DestroyMenu(menu);
    static const char feq_filter[] = "foobar2000 EQ presets (*.feq)|*.feq|All files|*.*";
    if (cmd == id_reset) {
        eq::set(eq::Values{});
    } else if (cmd == id_load_feq) {
        pfc::string8 path;
        if (uGetOpenFileName(m_wnd, feq_filter, 0, "feq", "Load EQ preset", nullptr, path, FALSE)) {
            if (!eq::load_feq(path)) popup_message::g_show(pfc::format("Not a foobar2000 EQ preset file:\n", path), "Equalizer", popup_message::icon_error);
        }
    } else if (cmd == id_save_feq) {
        pfc::string8 path;
        if (uGetOpenFileName(m_wnd, feq_filter, 0, "feq", "Save EQ preset", nullptr, path, TRUE)) {
            if (!eq::save_feq(path))
                popup_message::g_show(pfc::format("Cannot write file:\n", path), "Equalizer", popup_message::icon_error);
        }
    } else if (cmd >= id_preset_first && cmd < id_preset_first + list.size()) {
        eq::set(list[cmd - id_preset_first].values);
    }
    invalidate();
}

void EqWindow::render(Canvas& c) {
    if (m_shade) render_shade(c);
    else render_normal(c);
}

void EqWindow::render_normal(Canvas& c) {
    const Image& img = m_skin->bitmap(SkinBitmap::eqmain);
    const Image& ex = m_skin->bitmap(SkinBitmap::eq_ex);
    c.clear(0);
    c.blit(img, bg, { 0, 0 });
    c.blit(img, title_lit() ? title_active : title_inactive, { 0, 0 });
    const bool down = m_inside;
    if (m_pressed == Part::close && down) c.blit(img, close_down, pos_close);
    if (m_pressed == Part::shade && down) c.blit(ex, shade_btn_down, pos_shade_btn);

    const bool on = eq::enabled();
    const bool on_pressed = m_pressed == Part::on && down;
    c.blit(img, on ? (on_pressed ? on_on_down : on_on) : (on_pressed ? on_off_down : on_off), pos_on);
    const bool au = config::eq_auto.get();
    const bool au_pressed = m_pressed == Part::autob && down;
    c.blit(img, au ? (au_pressed ? auto_on_down : auto_on) : (au_pressed ? auto_off_down : auto_off), pos_auto);
    c.blit(img, m_pressed == Part::presets && down ? presets_down : presets_btn, pos_presets);

    const eq::Values vals = eq::get();
    auto slider = [&](int x, int v, bool pressed) {
        const int frame = static_cast<int>(std::lround(v / 100.0 * 27));
        c.blit(img, slider_frame_x + (frame % 14) * 15, slider_frame_y + (frame / 14) * 65, slider_w, slider_h, x, band_y);
        // Inverse of drag_to: 100..50 -> 0..25, 49..0 -> 26..51 (0 dB sits at 25, as in Winamp).
        constexpr int half = slider_travel / 2;
        const int ty = band_y + (v >= 50 ? static_cast<int>(std::lround((100 - v) * half / 50.0))
                                         : half + 1 + static_cast<int>(std::lround((49 - v) * half / 49.0)));
        c.blit(img, pressed ? thumb_down : thumb, { x + 1, ty });
    };
    slider(pos_preamp.x, vals.preamp, m_pressed == Part::preamp);
    for (int i = 0; i < eq::band_count; ++i)
        slider(slider_x(i), vals.bands[i], static_cast<int>(m_pressed) == static_cast<int>(Part::band0) + i);

    // Graph: background, preamp line, then the spline through the bands coloured per row.
    c.blit(img, graph_bg, pos_graph);
    constexpr int gh = 19;
    const int py = static_cast<int>(std::lround((100 - vals.preamp) / 100.0 * (gh - 1)));
    c.blit(img, preamp_line, { pos_graph.x, pos_graph.y + py });
    std::vector<double> xs, ys;
    for (int i = 0; i < eq::band_count; ++i) {
        xs.push_back(i * 12.0);
        ys.push_back((100 - vals.bands[i]) / 100.0 * (gh - 1));
    }
    const auto curve = spline(xs, ys);
    int last = std::clamp(static_cast<int>(std::lround(curve[0])), 0, gh - 1);
    for (size_t x = 0; x < curve.size(); ++x) {
        const int y = std::clamp(static_cast<int>(std::lround(curve[x])), 0, gh - 1);
        for (int yy = std::min(y, last); yy <= std::max(y, last); ++yy) {
            const bool have = img.height > graph_colors.y + yy && img.width > graph_colors.x;
            const uint32_t col = have ? img.pixels[size_t(graph_colors.y + yy) * img.width + graph_colors.x] : 0x00ff00u;
            c.set_pixel(pos_graph.x + 2 + static_cast<int>(x), pos_graph.y + yy, col);
        }
        last = y;
    }
}

void EqWindow::render_shade(Canvas& c) {
    const Image& ex = m_skin->bitmap(SkinBitmap::eq_ex);
    c.clear(0);
    c.blit(ex, title_lit() ? shade_active : shade_inactive, { 0, 0 });
    const bool down = m_inside;
    if (m_pressed == Part::shade && down) c.blit(ex, shade_unshade_down, pos_shade_btn);
    c.blit(ex, m_pressed == Part::close && down ? shade_close_down : shade_close, pos_close);

    auto thumb3 = [&](const S (&sprites)[3], double t, P pos, int w) {
        const S& s = t < 1.0 / 3 ? sprites[0] : t < 2.0 / 3 ? sprites[1] : sprites[2];
        c.blit(ex, s, { pos.x + static_cast<int>(std::lround(t * (w - 3))), pos.y });
    };
    thumb3(shade_vol_thumb, player::db_to_slider(playback_control::get()->get_volume()), pos_shade_vol, shade_vol_w);
    thumb3(shade_bal_thumb, (balance::get() + 1) / 2, pos_shade_bal, shade_bal_w);
}

} // namespace wsz
