#include <memory>

#include "api/geode_api.h"
#include "audio/dsp/Loudness.hpp"
#include "audio/dsp/PcmResampler.hpp"

struct geode_resampler {
    geode::audio::PcmResampler r;
};

struct geode_loudness {
    geode::audio::LoudnessAnalyser a;
};

extern "C" {

geode_resampler* geode_resampler_create(int in_rate, int out_rate, int channels) {
    if (in_rate <= 0 || out_rate <= 0 || channels <= 0) return nullptr;
    return new geode_resampler{geode::audio::PcmResampler(in_rate, out_rate, channels)};
}

void geode_resampler_destroy(geode_resampler* r) {
    std::unique_ptr<geode_resampler> owned(r);
}

int geode_resampler_channels(geode_resampler* r) {
    return r ? r->r.channels() : 0;
}

size_t geode_resampler_max_out_frames(geode_resampler* r, size_t in_frames) {
    return r ? r->r.maxOutFrames(in_frames) : 0;
}

int64_t geode_resampler_process(geode_resampler* r, const int16_t* in, size_t in_frames, int16_t* out,
                                size_t out_capacity_frames) {
    if (!r || in_frames == 0) return 0;
    if (!in || !out || out_capacity_frames < r->r.maxOutFrames(in_frames)) return -1;
    return static_cast<int64_t>(r->r.process(in, in_frames, out));
}

int geode_loudness_supports(int channels) {
    return geode::audio::LoudnessAnalyser::supports(channels) ? 1 : 0;
}

geode_loudness* geode_loudness_create(int sample_rate, int channels) {
    if (sample_rate <= 0 || !geode::audio::LoudnessAnalyser::supports(channels)) return nullptr;
    return new geode_loudness{geode::audio::LoudnessAnalyser(sample_rate, channels)};
}

void geode_loudness_destroy(geode_loudness* l) {
    std::unique_ptr<geode_loudness> owned(l);
}

void geode_loudness_feed(geode_loudness* l, const float* interleaved, size_t samples) {
    if (l && interleaved && samples > 0) l->a.feed(interleaved, samples);
}

int geode_loudness_has_block(geode_loudness* l) {
    return l && l->a.hasCompleteBlock() ? 1 : 0;
}

void geode_loudness_finish(geode_loudness* l, geode_loudness_report* out) {
    if (!out) return;
    *out = {};
    if (!l) return;
    const auto r = l->a.finish();
    out->has_integrated = r.hasIntegrated ? 1 : 0;
    out->integrated_lufs = r.integratedLufs;
    out->true_peak_dbtp = r.truePeakDbtp;
    out->sample_peak_dbfs = r.samplePeakDbfs;
    out->measured_ms = r.measuredMs;
    out->gated_block_count = r.gatedBlockCount;
}

}
