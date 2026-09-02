#include <algorithm>
#include <memory>

#include "api/geode_api.h"
#include "audio/dsp/DspChain.hpp"

struct geode_dsp {
    geode_dsp(int sampleRate, int channels) : chain(sampleRate, channels) {}

    geode::audio::DspChain chain;
};

namespace {
using geode::audio::Equalizer;
static_assert(Equalizer::kBands == GEODE_DSP_BANDS);
static_assert(Equalizer::kMinMillibels == GEODE_DSP_MIN_MILLIBELS);
static_assert(Equalizer::kMaxMillibels == GEODE_DSP_MAX_MILLIBELS);
}  // namespace

extern "C" {

geode_dsp* geode_dsp_create(int sample_rate, int channels) {
    if (sample_rate <= 0 || channels < 1 || channels > Equalizer::kMaxChannels) return nullptr;
    return std::make_unique<geode_dsp>(sample_rate, channels).release();
}

void geode_dsp_destroy(geode_dsp* d) {
    std::unique_ptr<geode_dsp> owned(d);
}

int geode_dsp_band_count(void) {
    return Equalizer::kBands;
}

float geode_dsp_band_center_hz(int band) {
    if (band < 0 || band >= Equalizer::kBands) return 0.0f;
    return Equalizer::centersHz()[static_cast<size_t>(band)];
}

void geode_dsp_set_enabled(geode_dsp* d, int enabled) {
    if (d) d->chain.equalizer().setEnabled(enabled != 0);
}

void geode_dsp_set_band(geode_dsp* d, int band, int millibels) {
    if (d) d->chain.equalizer().setBand(band, millibels);
}

int geode_dsp_band(geode_dsp* d, int band) {
    return d ? d->chain.equalizer().band(band) : 0;
}

void geode_dsp_set_bass_boost(geode_dsp* d, int permille) {
    if (d) d->chain.equalizer().setBassBoost(permille);
}

void geode_dsp_set_loudness_mb(geode_dsp* d, int millibels) {
    if (d) d->chain.loudness().setTargetDb(static_cast<float>(std::clamp(millibels, 0, 1000)) / 100.0f);
}

void geode_dsp_set_gain_db(geode_dsp* d, float db) {
    if (d) d->chain.gain().setTargetDb(db);
}

void geode_dsp_set_crossfeed(geode_dsp* d, int enabled) {
    if (d) d->chain.crossfeed().setEnabled(enabled != 0);
}

void geode_dsp_set_limiter(geode_dsp* d, int enabled) {
    if (d) d->chain.limiter().setEnabled(enabled != 0);
}

void geode_dsp_reset(geode_dsp* d) {
    if (d) d->chain.reset();
}

void geode_dsp_process(geode_dsp* d, float* interleaved, size_t frames) {
    if (d && interleaved && frames > 0) d->chain.process(interleaved, frames);
}

}
