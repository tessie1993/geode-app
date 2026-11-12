#include <algorithm>
#include <cmath>
#include <memory>

#include "api/geode_api.h"
#include "audio/dsp/DspProcessor.hpp"
#include "audio/dsp/EqPresets.hpp"
#include "audio/dsp/FxRanges.hpp"

struct geode_dsp {
    geode::audio::DspProcessor processor;
};

namespace {
using geode::audio::Equalizer;
static_assert(Equalizer::kBands == GEODE_DSP_BANDS);
static_assert(Equalizer::kMinMillibels == GEODE_DSP_MIN_MILLIBELS);
static_assert(Equalizer::kMaxMillibels == GEODE_DSP_MAX_MILLIBELS);
static_assert(geode::audio::kSilenceDb == GEODE_DSP_SILENCE_DB);
static_assert(geode::audio::fx::kBand == GEODE_DSP_FX_BAND && geode::audio::fx::kBassBoost == GEODE_DSP_FX_BASS_BOOST);
static_assert(geode::audio::fx::kLoudness == GEODE_DSP_FX_LOUDNESS);
static_assert(geode::audio::kTrackGain == GEODE_TAG_TRACK_GAIN && geode::audio::kTrackPeak == GEODE_TAG_TRACK_PEAK);
static_assert(geode::audio::kAlbumGain == GEODE_TAG_ALBUM_GAIN && geode::audio::kAlbumPeak == GEODE_TAG_ALBUM_PEAK);

// A tag value that is not a number counts as missing rather than reaching the gain.
int finiteMask(int mask, const float (&values)[4]) {
    int m = mask & (GEODE_TAG_TRACK_GAIN | GEODE_TAG_TRACK_PEAK | GEODE_TAG_ALBUM_GAIN | GEODE_TAG_ALBUM_PEAK);
    for (int bit = 0; bit < 4; ++bit) {
        if (!std::isfinite(values[bit])) m &= ~(1 << bit);
    }
    return m;
}
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

int geode_dsp_eq_preset_count(void) {
    return geode::audio::kEqPresets;
}

int geode_dsp_eq_preset_levels(int preset, int32_t* out, int capacity) {
    if (!out || preset < 0 || preset >= geode::audio::kEqPresets || capacity < Equalizer::kBands) return 0;
    const auto& levels = geode::audio::kEqPresetLevels[static_cast<size_t>(preset)];
    std::copy(levels.begin(), levels.end(), out);
    return Equalizer::kBands;
}

int geode_dsp_band_count(void) {
    return Equalizer::kBands;
}

int geode_dsp_fx_min(int control) {
    return geode::audio::fx::rangeOf(control).min;
}

int geode_dsp_fx_max(int control) {
    return geode::audio::fx::rangeOf(control).max;
}

int geode_dsp_fx_clamp(int control, int value) {
    return geode::audio::fx::clamp(control, value);
}

int geode_dsp_restore_bands(int preset, const int32_t* stored, int stored_count, int32_t* out, int capacity) {
    if (!out || capacity < Equalizer::kBands) return 0;
    geode::audio::fx::restoreBands(preset, stored, stored_count, out);
    return Equalizer::kBands;
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

void geode_dsp_set_crossfeed(geode_dsp* d, int enabled) {
    if (d) d->processor.setCrossfeed(enabled != 0);
}

void geode_dsp_set_limiter(geode_dsp* d, int enabled) {
    if (d) d->processor.setLimiter(enabled != 0);
}

void geode_dsp_ramp_to(geode_dsp* d, float db, int ms) {
    if (d) d->processor.gain().rampTo(db, ms);
}

int64_t geode_dsp_sleep_fade(geode_dsp* d, int64_t remaining_ms) {
    return d ? d->processor.gain().sleepFade(remaining_ms) : 1;
}

void geode_dsp_set_crossfade(geode_dsp* d, int ms) {
    if (d) d->processor.gain().setCrossfadeMs(ms);
}

void geode_dsp_set_follows_on(geode_dsp* d, int follows_on) {
    if (d) d->processor.gain().setFollowsOn(follows_on != 0);
}

void geode_dsp_set_speed(geode_dsp* d, float speed) {
    if (d) d->processor.gain().setSpeed(speed);
}

void geode_dsp_set_replaygain(geode_dsp* d, int mode, float preamp_db, int clip_guard) {
    if (d) d->processor.gain().replayGain().configure(mode, preamp_db, clip_guard != 0);
}

void geode_dsp_set_stream_tags(geode_dsp* d, const char* stream, int mask, float track_gain_db, float track_peak,
                               float album_gain_db, float album_peak) {
    if (!d) return;
    const float values[4] = {track_gain_db, track_peak, album_gain_db, album_peak};
    geode::audio::ReplayGainTags tags;
    tags.mask = finiteMask(mask, values);
    tags.trackGainDb = track_gain_db;
    tags.trackPeak = track_peak;
    tags.albumGainDb = album_gain_db;
    tags.albumPeak = album_peak;
    d->processor.gain().replayGain().setTags(geode::audio::streamKey(stream), tags);
}

void geode_dsp_begin_stream(geode_dsp* d, const char* stream, int64_t position_us, int64_t duration_us) {
    if (d) d->processor.gain().beginStream(geode::audio::streamKey(stream), position_us, duration_us);
}

void geode_dsp_end_stream(geode_dsp* d) {
    if (d) d->processor.gain().endStream();
}

void geode_dsp_process(geode_dsp* d, float* interleaved, size_t capacity_floats, size_t frames, int channels) {
    if (d) d->processor.process(interleaved, capacity_floats, frames, channels);
}

void geode_dsp_process_pcm16(geode_dsp* d, int16_t* interleaved, size_t capacity_samples, size_t frames, int channels) {
    if (d) d->processor.processPcm16(interleaved, capacity_samples, frames, channels);
}

void geode_dsp_set_bit_perfect(geode_dsp* d, int on) {
    if (d) d->processor.setBitPerfect(on != 0);
}

int geode_dsp_bit_perfect_live(const geode_dsp* d) {
    return d && d->processor.bitPerfectLive() ? 1 : 0;
}

}
