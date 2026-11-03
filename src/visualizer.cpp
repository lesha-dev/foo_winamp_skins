#include "stdafx.h"
#include "visualizer.h"

#include <cmath>
#include <complex>
#include <numbers>

// Behaviour follows Winamp's classic visualizer (studied from its published source; no code taken):
// - analyser: 512-sample Hann-windowed FFT of L+R, magnitudes * 0.5, folded into 75 values on a
//   2^(x/12) bin warp with Hermite-interpolated partial bins, clamped to 255;
// - bars: each 4-column group shows the integer mean of its 4 values, capped at 15; the bars stand
//   2 rows below the visible area, so 13 rows of a full bar show; colour = skin colour[17 - y];
// - falloff per cycle: bars drop 12/16 px, peaks drop by a speed that starts at 3/256 px and grows x1.1;
// - scope: 75 points, each the high byte of a sample / 8 (-16..16), drawn as connected lines.
namespace wsz {
namespace {

constexpr int fft_n = 512;
constexpr int scope_block = 576;      // samples spread over the 75 scope points
// Falloff cycles per second. Winamp's vis thread waits 16 ms per cycle without raising the timer
// resolution, so on a default 15.6 ms Windows tick each wait lasts ~31 ms: 32 cycles/s. This also
// matches the recorded peak hold (first 1 px drop after ~0.73 s).
constexpr double cycle_hz = 32.0;
constexpr int bar_drop = 12;          // 1/16 px per cycle
constexpr float peak_accel = 1.1f;
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

void Visualizer::step(Falloff& f, int column, int v) {
    v = std::min(v, 15);
    int& bar = f.bar[column];
    if (v * 16 < bar) v = (bar -= bar_drop) >> 4;
    else bar = v * 16;
    if (bar < 0) bar = 0;
    v = std::max(v, 0);
    f.shown[column] = v;
    int& peak = f.peak[column];
    float& speed = f.peak_speed[column];
    if (peak <= v * 256) {
        peak = v * 256;
        speed = 3.0f;
    }
    f.peak_shown[column] = peak; // Winamp draws the peak before decaying it
    peak -= static_cast<int>(speed);
    speed *= peak_accel;
    if (peak < 0) peak = 0;
}

bool Visualizer::tick(bool playing) {
    if (m_mode == VisMode::off) return false;
    const auto now = std::chrono::steady_clock::now();
    const double dt = m_last_tick == std::chrono::steady_clock::time_point{} ? 1.0 / cycle_hz
        : std::clamp(std::chrono::duration<double>(now - m_last_tick).count(), 0.0, 0.25);
    m_last_tick = now;

    if (m_mode == VisMode::oscilloscope) {
        m_scope_valid = playing && fetch_scope(m_scope);
        return playing;
    }

    if (!(playing && fetch_spectrum(m_spec))) m_spec.fill(0);
    m_pending += dt;
    while (m_pending >= 1.0 / cycle_hz) {
        m_pending -= 1.0 / cycle_hz;
        for (int x = 0; x < points; ++x) {
            const int t = x & ~3;
            step(m_normal, x, (m_spec[t] + m_spec[t + 1] + m_spec[t + 2] + m_spec[t + 3]) / 4);
        }
        for (int x = 0; x < 37; ++x) {
            const int t = (x & ~3) * 2;
            step(m_shade, x, (m_spec[t] + m_spec[t + 1] + m_spec[t + 2] + m_spec[t + 3]) / 4);
        }
    }
    if (playing) return true;
    for (int x = 0; x < points; ++x)
        if (m_normal.bar[x] > 0 || m_normal.peak[x] > 0 || m_shade.bar[x] > 0 || m_shade.peak[x] > 0) return true;
    return false;
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
                if ((x & 3) == 3) continue;
                const int v = std::min(m_shade.shown[x] * 5 / 15, 5);
                for (int y = 0; y < v; ++y) c.set_pixel(pos.x + x, pos.y + h - 1 - y, color(17 - y * 15 / 5));
                const int p = m_shade.peak_shown[x] * 5 / 15 / 256;
                if (p >= 0 && p <= 4) c.set_pixel(pos.x + x, pos.y + h - 1 - p, color(23));
            }
        } else if (m_scope_valid) {
            int last = -5;
            for (int x = 0; x < shade_width; ++x) {
                const int v = std::clamp((m_scope[x] + 8) * 5 / 16, 0, 4);
                const int lo = last == -5 ? v : std::min(v, last), hi = last == -5 ? v : std::max(v, last);
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
            if ((x & 3) == 3) continue;
            for (int y = base_offset; y < m_normal.shown[x]; ++y) c.set_pixel(pos.x + x, pos.y + row_of(y), color(17 - y));
            const int p = m_normal.peak_shown[x] / 256;
            if (p >= base_offset && p <= 15) c.set_pixel(pos.x + x, pos.y + row_of(p), color(23));
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
        // Rising: v down to prev inclusive; falling: v up to prev exclusive.
        const int lo = v >= prev ? prev : v, hi = v >= prev ? v : prev - 1;
        for (int y = lo; y <= hi; ++y)
            if (y >= base_offset) c.set_pixel(pos.x + x, pos.y + row_of(y), color(col));
    }
}

} // namespace wsz
