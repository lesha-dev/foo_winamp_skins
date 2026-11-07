#include "stdafx.h"
#include "visualizer.h"

#include <cmath>
#include <complex>
#include <numbers>

// Behaviour follows Winamp's classic visualizer:
// - analyser: 512-sample Hann-windowed FFT of L+R, magnitudes * 0.5, folded into 75 values on a
//   2^(x/12) bin warp with Hermite-interpolated partial bins, clamped to 255;
// - bars: each 4-column group shows the integer mean of its 4 values (thin bands: one value per
//   column), capped at 15; the bars stand 2 rows below the visible area, so 13 rows of a full bar
//   show; colour by style: normal 17 - y, fire v + 2 - y, line 17 - v;
// - falloff per cycle: bars drop {3,6,12,16,32}/16 px, peaks drop by a speed that starts at 3/256 px
//   and grows x{1.05,1.1,1.2,1.4,1.6}; the peak is drawn before that cycle's drop;
// - refresh rate: falloff runs every cycle, drawing every Nth cycle;
// - scope: 75 points, each the high byte of a sample / 8 (-16..16), as dots, lines or solid.
namespace wsz {
namespace {

constexpr int fft_n = 512;
constexpr int scope_block = 576;      // samples spread over the 75 scope points
// Falloff cycles per second. Winamp's vis thread waits 16 ms per cycle without raising the timer
// resolution, so on a default 15.6 ms Windows tick each wait lasts ~31 ms: 32 cycles/s. This also
// matches the recorded peak hold (first 1 px drop after ~0.73 s).
constexpr double cycle_hz = 32.0;
constexpr int bar_drop[5] = { 3, 6, 12, 16, 32 };              // 1/16 px per cycle
constexpr float peak_accel[5] = { 1.05f, 1.1f, 1.2f, 1.4f, 1.6f };
constexpr int base_offset = 2;        // bar/scope base sits 2 rows below the visible bottom

void fft(std::array<std::complex<double>, fft_n>& a) {
    for (int i = 1, j = 0; i < fft_n; ++i) {
        int bit = fft_n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(a[i], a[j]);
    }
    for (int len = 2; len <= fft_n; len <<= 1) {
        const std::complex<double> w = std::polar(1.0, -2.0 * std::numbers::pi / len);
        for (int i = 0; i < fft_n; i += len) {
            std::complex<double> wk = 1.0;
            for (int k = 0; k < len / 2; ++k, wk *= w) {
                const auto u = a[i + k], v = a[i + k + len / 2] * wk;
                a[i + k] = u + v;
                a[i + k + len / 2] = u - v;
            }
        }
    }
}

float hermite(float x, float y0, float y1, float y2, float y3) {
    const float c0 = y1;
    const float c1 = 0.5f * (y2 - y0);
    const float c3 = 1.5f * (y1 - y2) + 0.5f * (y3 - y0);
    const float c2 = y0 - y1 + c1 - c3;
    return ((c3 * x + c2) * x + c1) * x + c0;
}

// 256 magnitudes -> 75 analyser values.
void fold(const std::array<float, fft_n / 2>& mag, std::array<int, Visualizer::points + 4>& out) {
    const double scale = 255.0 / std::exp2(75.0 / 12.0);
    auto warp = [&](int x) { return static_cast<float>((std::exp2(x / 12.0) - 1.0) * scale) + 1.0f; };
    auto at = [&](int i) { return i >= 0 && i < fft_n / 2 ? mag[i] : 0.0f; };
    float next = warp(0);
    for (int x = 0; x < Visualizer::points; ++x) {
        float from = next;
        next = warp(x + 1);
        int bin = static_cast<int>(std::floor(from));
        const int end = std::min(static_cast<int>(std::floor(next)), fft_n / 2 - 1);
        float mult = static_cast<float>(bin + 1) - from;
        bool partial = true;
        float sum = 0;
        do {
            if (bin == end) { mult = next - from; partial = true; }
            if (partial) sum += hermite(from - bin, at(bin - 1), at(bin), at(bin + 1), at(bin + 2)) * mult;
            else sum += at(bin);
            partial = false;
            ++bin;
            from = static_cast<float>(bin);
        } while (bin <= end);
        out[x] = static_cast<int>(std::lrint(std::min(sum, 255.0f)));
    }
    for (int x = Visualizer::points; x < Visualizer::points + 4; ++x) out[x] = 0;
}

} // namespace

Visualizer::Visualizer() = default;
Visualizer::~Visualizer() = default;

void Visualizer::set_mode(VisMode m) {
    m_mode = m;
    reset();
    // fb2k only starts buffering vis data once a stream exists, so a fresh stream shows nothing for
    // a second or two. Create it as soon as the visualizer is on and keep it across mode changes.
    if (m != VisMode::off) ensure_stream();
}

void Visualizer::set_options(const VisOptions& o) {
    m_opt = o;
    m_opt.analyzer = std::clamp(m_opt.analyzer, 0, 2);
    m_opt.scope = std::clamp(m_opt.scope, 0, 2);
    m_opt.refresh = std::clamp(m_opt.refresh, 1, 4);
    m_opt.falloff = std::clamp(m_opt.falloff, 0, 4);
    m_opt.peak_falloff = std::clamp(m_opt.peak_falloff, 0, 4);
    m_redraw = true;
}

void Visualizer::release() {
    m_stream.release();
}

void Visualizer::reset() {
    m_normal = {};
    m_shade = {};
    m_spec.fill(0);
    m_scope.fill(0);
    m_scope_valid = false;
    m_last_tick = {};
    m_pending = 0;
    m_cycle = 0;
    m_redraw = true;
}

void Visualizer::ensure_stream() {
    if (m_stream.is_valid()) return;
    try {
        visualisation_manager::get()->create_stream(m_stream, 0);
        m_stream->set_channel_mode(visualisation_stream_v2::channel_mode_mono);
        m_stream->request_backlog(0.5);
    } catch (const std::exception&) {
        m_stream.release();
    }
}

bool Visualizer::fetch_spectrum(std::array<int, points + 4>& out) {
    double t;
    if (!m_stream.is_valid() || !m_stream->get_absolute_time(t)) return false;
    const unsigned rate = m_chunk.get_sample_rate() ? m_chunk.get_sample_rate() : 44100;
    // Centred on the playback time: the data right up to "now" may not be complete yet, and a short
    // chunk would be zero-padded and read low. Fall back to the window ending at "now".
    const double len = static_cast<double>(fft_n) / rate;
    if (!m_stream->get_chunk_absolute(m_chunk, t - len / 2, len) || m_chunk.get_sample_count() < static_cast<size_t>(fft_n))
        if (!m_stream->get_chunk_absolute(m_chunk, t - len, len)) return false;
    const size_t n = m_chunk.get_sample_count();
    const unsigned ch = std::max(1u, m_chunk.get_channels());
    const audio_sample* data = m_chunk.get_data();
    if (!n || !data) return false;
    std::array<std::complex<double>, fft_n> a{};
    for (int i = 0; i < fft_n; ++i) {
        const ptrdiff_t s = static_cast<ptrdiff_t>(n) - fft_n + i; // last 512 samples, zero-padded in front
        if (s < 0) continue;
        double v = 0;
        for (unsigned c = 0; c < ch; ++c) v += data[s * ch + c];
        v = v / ch * 2.0; // mono mix -> L+R
        const double w = 0.5 - 0.5 * std::cos(2.0 * std::numbers::pi * i / (fft_n - 1));
        a[i] = v * w;
    }
    fft(a);
    std::array<float, fft_n / 2> mag{};
    // Bin 0 combines DC and Nyquist (packed real FFT layout).
    mag[0] = static_cast<float>(std::hypot(a[0].real(), a[fft_n / 2].real()) * 0.5);
    for (int i = 1; i < fft_n / 2; ++i) mag[i] = static_cast<float>(std::abs(a[i]) * 0.5);
    fold(mag, out);
    return true;
}

bool Visualizer::fetch_scope(std::array<int, points>& out) {
    double t;
    if (!m_stream.is_valid() || !m_stream->get_absolute_time(t)) return false;
    const unsigned rate = m_chunk.get_sample_rate() ? m_chunk.get_sample_rate() : 44100;
    const double len = static_cast<double>(scope_block) / rate;
    if (!m_stream->get_chunk_absolute(m_chunk, t - len / 2, len)) return false;
    const size_t n = m_chunk.get_sample_count();
    const unsigned ch = std::max(1u, m_chunk.get_channels());
    const audio_sample* data = m_chunk.get_data();
    if (n < static_cast<size_t>(points) || !data) return false;
    for (int x = 0; x < points; ++x) {
        const size_t s = n * x / points;
        double v = 0;
        for (unsigned c = 0; c < ch; ++c) {
            const int pcm = std::clamp(static_cast<int>(std::lrint(data[s * ch + c] * 32768.0)), -32768, 32767);
            v += (pcm >> 8) / 8.0; // high byte, -16..16
        }
        out[x] = static_cast<int>(std::lrint(v / ch));
    }
    return true;
}

void Visualizer::step(Falloff& f, int column, int v, bool draw) {
    v = std::min(v, 15);
    int& bar = f.bar[column];
    if (v * 16 < bar) v = (bar -= bar_drop[m_opt.falloff]) >> 4;
    else bar = v * 16;
    if (bar < 0) bar = 0;
    v = std::max(v, 0);
    if (draw) f.shown[column] = v;
    int& peak = f.peak[column];
    float& speed = f.peak_speed[column];
    if (peak <= v * 256) {
        peak = v * 256;
        speed = 3.0f;
    }
    if (draw) f.peak_shown[column] = peak; // Winamp draws the peak before decaying it
    peak -= static_cast<int>(speed);
    speed *= peak_accel[m_opt.peak_falloff];
    if (peak < 0) peak = 0;
}

bool Visualizer::tick(bool playing) {
    if (m_mode == VisMode::off) return false;
    const auto now = std::chrono::steady_clock::now();
    const double dt = m_last_tick == std::chrono::steady_clock::time_point{} ? 1.0 / cycle_hz
        : std::clamp(std::chrono::duration<double>(now - m_last_tick).count(), 0.0, 0.25);
    m_last_tick = now;

    const bool spectrum = m_mode == VisMode::spectrum;
    bool fetched = false;
    m_pending += dt;
    while (m_pending >= 1.0 / cycle_hz) {
        m_pending -= 1.0 / cycle_hz;
        const bool draw = ++m_cycle >= m_opt.refresh;
        if (draw) {
            m_cycle = 0;
            m_redraw = true;
        }
        if (!spectrum) {
            if (draw) m_scope_valid = playing && fetch_scope(m_scope);
            continue;
        }
        if (!fetched) {
            if (!(playing && fetch_spectrum(m_spec))) m_spec.fill(0);
            fetched = true;
        }
        const auto& s = m_spec;
        for (int x = 0; x < points; ++x) {
            const int t = x & ~3;
            step(m_normal, x, m_opt.thin_bands ? s[x] : (s[t] + s[t + 1] + s[t + 2] + s[t + 3]) / 4, draw);
        }
        for (int x = 0; x < 37; ++x) {
            const int t = (x & ~3) * 2;
            step(m_shade, x, m_opt.thin_bands ? (s[x * 2] + s[x * 2 + 1]) / 2 : (s[t] + s[t + 1] + s[t + 2] + s[t + 3]) / 4, draw);
        }
    }
    if (!spectrum) return playing;
    if (playing) return true;
    for (int x = 0; x < points; ++x)
        if (m_normal.bar[x] > 0 || m_normal.peak[x] > 0 || m_shade.bar[x] > 0 || m_shade.peak[x] > 0) return true;
    return false;
}

// Colour index of base row 0 of a bar of height v; rows above count down from it (line style: whole bar).
int Visualizer::bar_color(int v) const {
    switch (m_opt.analyzer) {
    case 1: return v + 2;  // fire
    case 2: return 17 - v; // line
    default: return 17;
    }
}

void Visualizer::render(Canvas& c, const Skin& skin, layout::Pos pos, bool shade) const {
    if (m_mode == VisMode::off) return;
    const auto& colors = skin.vis_colors();
    auto color = [&](int i) { return to_bgrx(colors[std::clamp(i, 0, 23)]); };

    if (shade) {
        const int h = shade_height;
        c.fill_rect(pos.x, pos.y, shade_width, h, color(0));
        if (m_mode == VisMode::spectrum) {
            for (int x = 0; x < 37; ++x) {
                if (!m_opt.thin_bands && (x & 3) == 3) continue;
                const int full = m_shade.shown[x];
                const int top = bar_color(full);
                const int v = std::min(full * 5 / 15, 5);
                for (int y = 0; y < v; ++y)
                    c.set_pixel(pos.x + x, pos.y + h - 1 - y, color(m_opt.analyzer == 2 ? top : top - y * 15 / 5));
                const int p = m_shade.peak_shown[x] * 5 / 15 / 256;
                if (m_opt.peaks && p >= 0 && p <= 4) c.set_pixel(pos.x + x, pos.y + h - 1 - p, color(23));
            }
        } else if (m_scope_valid) {
            int last = -5;
            for (int x = 0; x < shade_width; ++x) {
                const int v = std::clamp((m_scope[x] + 8) * 5 / 16, 0, 4);
                int lo = v, hi = v;
                if (m_opt.scope == 2) lo = std::min(v, 2), hi = std::max(v, 2);
                else if (m_opt.scope == 1 && last != -5) lo = std::min(v, last), hi = std::max(v, last);
                for (int y = lo; y <= hi; ++y) c.set_pixel(pos.x + x, pos.y + h - 1 - y, color(18));
                last = v;
            }
        }
        return;
    }

    // Background: colour 0 with colour-1 dots on every other row and column.
    c.fill_rect(pos.x, pos.y, width, height, color(0));
    for (int row = 1; row < height; row += 2)
        for (int x = 0; x < width; x += 2) c.set_pixel(pos.x + x, pos.y + row, color(1));

    // Base row y (0 = 2 rows below the visible bottom) -> visible row from the top.
    auto row_of = [](int y) { return height - 1 + base_offset - y; };

    if (m_mode == VisMode::spectrum) {
        for (int x = 0; x < points; ++x) {
            if (!m_opt.thin_bands && (x & 3) == 3) continue;
            const int v = m_normal.shown[x];
            const int top = bar_color(v);
            for (int y = base_offset; y < v; ++y)
                c.set_pixel(pos.x + x, pos.y + row_of(y), color(m_opt.analyzer == 2 ? top : top - y));
            const int p = m_normal.peak_shown[x] / 256;
            if (m_opt.peaks && p >= base_offset && p <= 15) c.set_pixel(pos.x + x, pos.y + row_of(p), color(23));
        }
        return;
    }

    if (!m_scope_valid) return;
    int last = -1;
    for (int x = 0; x < points; ++x) {
        int v = std::clamp(m_scope[x] + 8, 0, 15);
        const int col = 18 + std::abs(v / 2 - 4);
        if (last == -1) last = v;
        const int prev = last;
        last = v;
        int lo = v, hi = v;
        if (m_opt.scope == 1) {
            // Rising: v down to prev inclusive; falling: v up to prev exclusive.
            lo = v >= prev ? prev : v;
            hi = v >= prev ? v : prev - 1;
        } else if (m_opt.scope == 2) {
            lo = v > 7 ? 8 : v;
            hi = v > 7 ? v : 7;
        }
        for (int y = lo; y <= hi; ++y)
            if (y >= base_offset) c.set_pixel(pos.x + x, pos.y + row_of(y), color(col));
    }
}

} // namespace wsz
