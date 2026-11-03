#include "stdafx.h"
#include "balance_dsp.h"
#include "config.h"
#include "guids.h"
#include "version.h"

#include <atomic>
#include <bit>
#include <cmath>

namespace wsz::balance {
namespace {

std::atomic<float> g_value{ 0.0f };
std::once_flag g_loaded;

float current() {
    std::call_once(g_loaded, [] { g_value = static_cast<float>(std::clamp(config::balance.get(), -1.0, 1.0)); });
    return g_value.load(std::memory_order_relaxed);
}

int channel_index(unsigned config, unsigned mask) {
    if (!(config & mask)) return -1;
    return std::popcount(config & (mask - 1));
}

// Winamp-style balance: moving towards one side attenuates the opposite front channel linearly.
// Stays in the chain permanently and passes audio through untouched while centred.
class BalanceDsp : public dsp_impl_base {
public:
    static GUID g_get_guid() { return guids::balance_dsp; }
    static void g_get_name(pfc::string_base& out) { out = COMPONENT_NAME " balance"; }

    bool on_chunk(audio_chunk* chunk, abort_callback&) override {
        const float b = current();
        if (std::abs(b) < 1e-4f) return true;
        const unsigned cfg = chunk->get_channel_config();
        const unsigned channels = chunk->get_channels();
        const int idx = channel_index(cfg, b < 0 ? audio_chunk::channel_front_right : audio_chunk::channel_front_left);
        if (idx < 0 || channels < 2) return true;
        const audio_sample gain = static_cast<audio_sample>(1.0f - std::abs(b));
        audio_sample* data = chunk->get_data();
        const size_t samples = chunk->get_sample_count();
        for (size_t s = 0; s < samples; ++s) data[s * channels + idx] *= gain;
        return true;
    }

    void on_endofplayback(abort_callback&) override {}
    void on_endoftrack(abort_callback&) override {}
    void flush() override {}
    double get_latency() override { return 0; }
    bool need_track_change_mark() override { return false; }
};

dsp_factory_nopreset_t<BalanceDsp> g_dsp_factory;

} // namespace

double get() { return current(); }

void set(double value) {
    current(); // make sure the stored value is loaded before it gets overwritten
    value = std::clamp(value, -1.0, 1.0);
    g_value.store(static_cast<float>(value), std::memory_order_relaxed);
    config::balance = value;
}

bool enabled() { return config::balance_dsp.get(); }

void set_enabled(bool on) {
    config::balance_dsp = on;
    if (on) return; // re-inserted on the next off-centre slider release
    set(0);
    dsp_config_manager::get()->core_disable_dsp(guids::balance_dsp);
}

void ensure_dsp() {
    if (!enabled()) return;
    auto mgr = dsp_config_manager::get();
    dsp_preset_impl preset;
    if (mgr->core_query_dsp(guids::balance_dsp, preset)) return;
    if (!dsp_entry::g_get_default_preset(preset, guids::balance_dsp)) return;
    mgr->core_enable_dsp(preset, dsp_config_manager::default_insert_last);
}

} // namespace wsz::balance
