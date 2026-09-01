#include "api/geode_api.h"

#include <algorithm>
#include <cstring>
#include <memory>

#include "analysis/AnalysisSession.hpp"
#include "analysis/DrumChannels.hpp"
#include "analysis/PulseReplay.hpp"

static_assert(sizeof(GeodeFeatureFrame) == GEODE_FEATURE_FRAME_FLOATS * sizeof(float));
static_assert(sizeof(geode::analysis::pulse::Frame) == 6 * sizeof(float));

struct geode_analysis {
    geode::analysis::AnalysisSession session;
};

struct geode_drums {
    geode::analysis::DrumChannels channels;
};

extern "C" {

const char* geode_version(void) {
    return "1.7.0";
}

geode_analysis* geode_analysis_create(int sample_rate, int fft_size, float hop_rate_hz) {
    if (sample_rate <= 0 || fft_size < 2 || (fft_size & (fft_size - 1)) != 0 || hop_rate_hz <= 0.0f) return nullptr;
    return std::make_unique<geode_analysis>(geode_analysis{geode::analysis::AnalysisSession(sample_rate, fft_size, hop_rate_hz)}).release();
}

void geode_analysis_destroy(geode_analysis* a) {
    std::unique_ptr<geode_analysis> owned(a);
}

void geode_analysis_set_sample_rate(geode_analysis* a, int sample_rate) {
    if (a) a->session.setSampleRateHz(sample_rate);
}

void geode_analysis_set_tuning(geode_analysis* a, float sensitivity, float refractory_ms, float attack_seconds,
                               float release_seconds) {
    if (a) a->session.setTuning(sensitivity, refractory_ms, attack_seconds, release_seconds);
}

void geode_analysis_reset(geode_analysis* a) {
    if (a) a->session.reset();
}

void geode_analysis_analyze(geode_analysis* a, const float* mid, const float* side, float dt_seconds, GeodeFeatureFrame* out) {
    if (a && mid && out) a->session.analyze(mid, side, dt_seconds, *out);
}

void geode_analysis_push(geode_analysis* a, const float* interleaved, size_t frames, int channels) {
    if (a && interleaved) a->session.push(interleaved, frames, channels);
}

int geode_analysis_pull(geode_analysis* a, GeodeFeatureFrame* out) {
    return (a && out && a->session.pull(*out)) ? 1 : 0;
}

size_t geode_analysis_key(geode_analysis* a, char* out, size_t capacity) {
    if (!a || !out || capacity == 0) return 0;
    const std::string key = a->session.key();
    const size_t n = std::min(key.size(), capacity - 1);
    std::memcpy(out, key.data(), n);
    out[n] = '\0';
    return n;
}

void geode_pulse_replay(const float* flux, size_t count, const float* rms, size_t rms_count, float hop_rate_hz,
                        float sensitivity, float refractory_ms, float* out) {
    if (!flux || !out || count == 0 || hop_rate_hz <= 0.0f) return;
    geode::analysis::pulse::decide(flux, count, rms, rms_count, hop_rate_hz, sensitivity, refractory_ms,
                                   reinterpret_cast<geode::analysis::pulse::Frame*>(out));
}

geode_drums* geode_drums_create(int band_count, float hop_rate_hz, int sample_rate) {
    if (band_count <= 0 || hop_rate_hz <= 0.0f || sample_rate <= 0) return nullptr;
    return std::make_unique<geode_drums>(geode_drums{geode::analysis::DrumChannels(band_count, hop_rate_hz, sample_rate)}).release();
}

void geode_drums_destroy(geode_drums* d) {
    std::unique_ptr<geode_drums> owned(d);
}

void geode_drums_step(geode_drums* d, const float* bands, float* kick_snare_hat) {
    if (!d || !bands || !kick_snare_hat) return;
    d->channels.step(bands);
    kick_snare_hat[0] = d->channels.kick();
    kick_snare_hat[1] = d->channels.snare();
    kick_snare_hat[2] = d->channels.hat();
}

}
