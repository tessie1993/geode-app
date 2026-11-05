#include <algorithm>
#include <memory>

#include "api/geode_api.h"
#include "audio/dsp/DspProcessor.hpp"

struct geode_dsp {
    geode::audio::DspProcessor processor;
};

namespace {
using geode::audio::Equalizer;
static_assert(Equalizer::kBands == GEODE_DSP_BANDS);
static_assert(Equalizer::kMinMillibels == GEODE_DSP_MIN_MILLIBELS);
static_assert(Equalizer::kMaxMillibels == GEODE_DSP_MAX_MILLIBELS);
}  // namespace

extern "C" {

geode_dsp* geode_dsp_create(void) {
    return std::make_unique<geode_dsp>().release();
}

void geode_dsp_destroy(geode_dsp* d) {
    std::unique_ptr<geode_dsp> owned(d);
}

int geode_dsp_configure(geode_dsp* d, int sample_rate, int channels) {
    return d && d->processor.configure(sample_rate, channels) ? 1 : 0;
}

void geode_dsp_flush(geode_dsp* d) {
    if (d) d->processor.flush();
}

float geode_dsp_band_center_hz(int band) {
    if (band < 0 || band >= Equalizer::kBands) return 0.0f;
    return Equalizer::centersHz()[static_cast<size_t>(band)];
}

void geode_dsp_set_enabled(geode_dsp* d, int enabled) {
    if (d) d->processor.setEnabled(enabled != 0);
}

void geode_dsp_set_band(geode_dsp* d, int band, int millibels) {
    if (d) d->processor.setBand(band, millibels);
}

void geode_dsp_set_bass_boost(geode_dsp* d, int permille) {
    if (d) d->processor.setBassBoost(permille);
}

void geode_dsp_set_loudness_mb(geode_dsp* d, int millibels) {
    if (d) d->processor.setLoudnessMb(millibels);
}

void geode_dsp_set_gain_db(geode_dsp* d, float db) {
    if (d) d->processor.setGainDb(db);
}

void geode_dsp_set_crossfeed(geode_dsp* d, int enabled) {
    if (d) d->processor.setCrossfeed(enabled != 0);
}

void geode_dsp_set_limiter(geode_dsp* d, int enabled) {
    if (d) d->processor.setLimiter(enabled != 0);
}

void geode_dsp_process(geode_dsp* d, float* interleaved, size_t capacity_floats, size_t frames, int channels) {
    if (d) d->processor.process(interleaved, capacity_floats, frames, channels);
}

}
