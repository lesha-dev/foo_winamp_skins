#include "stdafx.h"
#include "visualizer.h"

#include <cmath>

namespace wsz {
namespace {

constexpr unsigned fft_size = 2048;
constexpr double osc_length = 0.025;   // seconds of PCM shown by the oscilloscope
constexpr float db_floor = -60.0f;     // bar height 0
constexpr float db_ceil = -6.0f;       // bar height full
constexpr float bar_fall = 0.06f;      // per frame
constexpr float peak_gravity = 0.0015f; // peak drop = gravity * frames^2 (Webamp-like)

uint32_t to_bgrx(COLORREF c) { return (GetRValue(c) << 16) | (GetGValue(c) << 8) | GetBValue(c); }

// Bin index bounds for each bar: first bar 0..200 Hz, then log-spaced up to Nyquist (Webamp's layout).
struct Bands { std::array<unsigned, Visualizer::bar_count + 1> edge; };

Bands make_bands(unsigned bins, double rate) {
    Bands b{};
    constexpr double min_hz = 200;
    const double max_hz = rate / 2;
    const double step = std::pow(max_hz / min_hz, 1.0 / (Visualizer::bar_count - 1));
    double hz = min_hz;
    b.edge[0] = 0;
    for (int i = 1; i <= Visualizer::bar_count; ++i, hz *= step) {
        unsigned idx = static_cast<unsigned>(std::min(hz, max_hz) / max_hz * bins);
        b.edge[i] = std::clamp(idx, b.edge[i - 1] + 1, bins);
    }
    return b;
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
    m_bars.fill(0);
    m_peaks.fill(0);
    m_peak_frames.fill(0);
    m_osc.fill(0);
    m_osc_valid = false;
}

void Visualizer::ensure_stream() {
    if (m_stream.is_valid()) return;
    try {
        visualisation_manager::get()->create_stream(m_stream, visualisation_manager::KStreamFlagNewFFT);
        m_stream->set_channel_mode(visualisation_stream_v2::channel_mode_mono);
        m_stream->request_backlog(0.5);
    } catch (const std::exception&) {
        m_stream.release();
    }
}

bool Visualizer::tick(bool playing) {
    if (m_mode == VisMode::off) return false;
    if (m_mode == VisMode::spectrum) {
        update_spectrum(playing);
        if (playing) return true;
        for (int i = 0; i < bar_count; ++i)
            if (m_bars[i] > 0 || m_peaks[i] > 0) return true;
        return false;
    }
    update_oscilloscope(playing);
    return playing;
}

void Visualizer::update_spectrum(bool have_data) {
    std::array<float, bar_count> target{};
    double t;
    if (have_data && m_stream.is_valid() && m_stream->get_absolute_time(t) && m_stream->get_spectrum_absolute(m_chunk, t, fft_size)) {
        const unsigned bins = static_cast<unsigned>(m_chunk.get_sample_count());
        const unsigned ch = std::max(1u, m_chunk.get_channels());
        const audio_sample* data = m_chunk.get_data();
        if (bins > bar_count && data) {
            const Bands bands = make_bands(bins, m_chunk.get_sample_rate());
            for (int i = 0; i < bar_count; ++i) {
                float peak = 0;
                for (unsigned k = bands.edge[i]; k < bands.edge[i + 1]; ++k)
                    for (unsigned c = 0; c < ch; ++c) peak = std::max(peak, static_cast<float>(std::abs(data[k * ch + c])));
                const float db = peak > 0 ? 20.0f * std::log10(peak) : db_floor;
                target[i] = std::clamp((db - db_floor) / (db_ceil - db_floor), 0.0f, 1.0f);
            }
        }
    }
    for (int i = 0; i < bar_count; ++i) {
        m_bars[i] = std::max(target[i], m_bars[i] - bar_fall);
        float peak = m_peaks[i] - peak_gravity * static_cast<float>(m_peak_frames[i] * m_peak_frames[i]);
        if (peak <= m_bars[i]) {
            peak = m_bars[i];
            m_peak_frames[i] = 0;
        } else {
            ++m_peak_frames[i];
        }
        m_peaks[i] = std::max(0.0f, peak);
    }
}

void Visualizer::update_oscilloscope(bool have_data) {
    m_osc_valid = false;
    double t;
    if (!have_data || !m_stream.is_valid() || !m_stream->get_absolute_time(t)) return;
    if (!m_stream->get_chunk_absolute(m_chunk, t - osc_length / 2, osc_length)) return;
    const size_t n = m_chunk.get_sample_count();
    const unsigned ch = std::max(1u, m_chunk.get_channels());
    const audio_sample* data = m_chunk.get_data();
    if (n < static_cast<size_t>(width) || !data) return;
    for (int x = 0; x < width; ++x) {
        const size_t from = n * x / width, to = n * (x + 1) / width;
        double sum = 0;
        for (size_t s = from; s < to; ++s)
            for (unsigned c = 0; c < ch; ++c) sum += data[s * ch + c];
        m_osc[x] = static_cast<float>(sum / static_cast<double>((to - from) * ch));
    }
    m_osc_valid = true;
}

void Visualizer::render(Canvas& c, const Skin& skin, layout::Pos pos, bool shade) const {
    if (m_mode == VisMode::off) return;
    const auto& colors = skin.vis_colors();
    const int w = shade ? shade_width : width, h = shade ? shade_height : height;

    // Background: color 0, plus a dot grid in color 1 (normal mode only).
    c.fill_rect(pos.x, pos.y, w, h, to_bgrx(colors[0]));
    if (!shade) {
        const uint32_t dot = to_bgrx(colors[1]);
        for (int y = 1; y < h; y += 2)
            for (int x = 0; x < w; x += 2) c.set_pixel(pos.x + x, pos.y + y, dot);
    }

    if (m_mode == VisMode::spectrum) {
        // Colors 2..17 are the bar gradient from top to bottom; 23 is the peak marker.
        const int bar_w = shade ? 1 : 3, bar_step = shade ? 2 : 4;
        const uint32_t peak_color = to_bgrx(colors[23]);
        for (int i = 0; i < bar_count; ++i) {
            const int x = pos.x + i * bar_step;
            const int bh = static_cast<int>(std::ceil(m_bars[i] * h - 0.001f));
            for (int r = 0; r < bh; ++r) {
                const int idx = 2 + 15 - r * 16 / h;
                c.fill_rect(x, pos.y + h - 1 - r, bar_w, 1, to_bgrx(colors[idx]));
            }
            if (!shade) {
                const int ph = static_cast<int>(std::ceil(m_peaks[i] * h - 0.001f));
                if (ph > 0) c.fill_rect(x, pos.y + h - ph, bar_w, 1, peak_color);
            }
        }
        return;
    }

    if (!m_osc_valid) return;
    // Oscilloscope "lines" style. Colors 18..22 go from the centre line outwards.
    auto row = [&](float v) { return std::clamp(static_cast<int>(std::lround((1.0f - v) * 0.5f * (h - 1))), 0, h - 1); };
    auto color_for = [&](int y) {
        const double d = std::abs(y - (h - 1) / 2.0) / (h / 2.0); // 0 centre .. 1 edge
        return to_bgrx(colors[18 + std::min(4, static_cast<int>(d * 5))]);
    };
    int prev = row(m_osc[0]);
    for (int x = 0; x < w; ++x) {
        const int src = shade ? x * width / w : x;
        const int y = row(m_osc[src]);
        const int lo = std::min(prev, y), hi = std::max(prev, y);
        for (int yy = (x == 0 ? y : lo); yy <= hi; ++yy) c.set_pixel(pos.x + x, pos.y + yy, color_for(yy));
        prev = y;
    }
}

} // namespace wsz
