#include <algorithm>
#include <cmath>
#include <memory>

#include "api/geode_api.h"
#include "audio/dsp/DspChain.hpp"

struct geode_replaygain {
    geode::audio::ReplayGainTable table;
};

struct geode_dsp {
    geode_dsp(int sampleRate, int channels) : chain(sampleRate, channels) {}

    geode::audio::DspChain chain;
};

namespace {
using geode::audio::Equalizer;
static_assert(Equalizer::kBands == GEODE_DSP_BANDS);
static_assert(Equalizer::kMinMillibels == GEODE_DSP_MIN_MILLIBELS);
static_assert(Equalizer::kMaxMillibels == GEODE_DSP_MAX_MILLIBELS);
static_assert(geode::audio::kTrackGain == GEODE_TAG_TRACK_GAIN && geode::audio::kTrackPeak == GEODE_TAG_TRACK_PEAK);
static_assert(geode::audio::kAlbumGain == GEODE_TAG_ALBUM_GAIN && geode::audio::kAlbumPeak == GEODE_TAG_ALBUM_PEAK);
}  // namespace

extern "C" {

geode_dsp* geode_dsp_create(int sample_rate, int channels) {
    if (sample_rate <= 0 || channels < 1 || channels > Equalizer::kMaxChannels) return nullptr;
    return std::make_unique<geode_dsp>(sample_rate, channels).release();
}

void geode_dsp_destroy(geode_dsp* d) {
    std::unique_ptr<geode_dsp> owned(d);
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

void geode_dsp_set_bass_boost(geode_dsp* d, int permille) {
    if (d) d->chain.equalizer().setBassBoost(permille);
}

void geode_dsp_set_loudness_mb(geode_dsp* d, int millibels) {
    if (d) d->chain.loudness().setTargetDb(static_cast<float>(std::clamp(millibels, 0, 1000)) / 100.0f);
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

geode_replaygain* geode_replaygain_create(void) { return std::make_unique<geode_replaygain>().release(); }

void geode_replaygain_destroy(geode_replaygain* r) { std::unique_ptr<geode_replaygain> owned(r); }

void geode_replaygain_configure(geode_replaygain* r, int mode, float preamp_db, int clip_guard) {
    if (r) r->table.configure(mode, std::isfinite(preamp_db) ? preamp_db : 0.0f, clip_guard != 0);
}

void geode_replaygain_set_tags(geode_replaygain* r, const char* stream, int mask, const float values[4]) {
    if (!r) return;
    geode::audio::ReplayGainTags tags;
    if (values) {
        tags.mask = mask & (GEODE_TAG_TRACK_GAIN | GEODE_TAG_TRACK_PEAK | GEODE_TAG_ALBUM_GAIN | GEODE_TAG_ALBUM_PEAK);
        tags.trackGainDb = values[0];
        tags.trackPeak = values[1];
        tags.albumGainDb = values[2];
        tags.albumPeak = values[3];
        // A value that is not a number is treated as missing rather than reaching the gain stage.
        for (int bit = 0; bit < 4; ++bit) {
            if (!std::isfinite(values[bit])) tags.mask &= ~(1 << bit);
        }
    }
    r->table.setTags(geode::audio::replayGainStreamKey(stream), tags);
}

void geode_dsp_set_replaygain(geode_dsp* d, const geode_replaygain* r) {
    if (d) d->chain.setReplayGain(r ? &r->table : nullptr);
}

void geode_dsp_begin_stream(geode_dsp* d, const char* stream) {
    if (d) d->chain.beginStream(geode::audio::replayGainStreamKey(stream));
}

void geode_dsp_process(geode_dsp* d, float* interleaved, size_t frames) {
    if (d && interleaved && frames > 0) d->chain.process(interleaved, frames);
}

}
