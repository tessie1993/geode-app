// The C ABI over the export planning and PCM maths (geode_api.h, "Export planning and PCM maths"). Only
// argument checks and copies live here; the rules are in output/ and audio/dsp/PcmConvert and PcmTiming,
// each host-tested in core/tests.
#include <algorithm>

#include "api/geode_api.h"
#include "audio/dsp/PcmConvert.hpp"
#include "audio/dsp/PcmTiming.hpp"
#include "output/EncodePlan.hpp"
#include "output/FrameClock.hpp"
#include "output/LoopPlan.hpp"
#include "output/LoudnessAdvice.hpp"
#include "output/PreRoll.hpp"
#include "output/RecordPlan.hpp"

namespace out = geode::output;

namespace {

void fill(const out::LoopSpec& s, geode_loop_spec* o) {
    o->loop_ms = s.loopMs;
    o->crossfade_ms = s.crossfadeMs;
    o->fps = s.fps;
    o->loop_frames = s.loopFrames();
    o->crossfade_frames = s.crossfadeFrames();
    o->source_frames = s.sourceFrames();
    o->loop_duration_us = s.loopDurationUs();
}

}  // namespace

extern "C" {

int64_t geode_export_total_frames(int64_t duration_us, int fps) { return out::FrameClock(fps, 0).framesFor(duration_us); }

int64_t geode_export_frame_pts_ns(int64_t frame, int fps) { return out::FrameClock(fps, 0).ptsNs(frame); }

int64_t geode_export_frame_time_ms(int64_t frame, int fps) { return out::FrameClock(fps, 0).frameTimeMs(frame); }

void geode_export_pre_roll(int64_t range_start_ms, int64_t* from_ms, int64_t* heard_ms) {
    const out::PreRoll plan = out::preRollFor(range_start_ms);
    if (from_ms) *from_ms = plan.fromMs;
    if (heard_ms) *heard_ms = plan.heardMs;
}

void geode_loop_spec_of(int64_t loop_ms, int64_t crossfade_ms, int fps, geode_loop_spec* o) {
    if (o) fill(out::LoopSpec::of(loop_ms, crossfade_ms, fps), o);
}

void geode_loop_spec_fill(geode_loop_spec* spec) {
    if (!spec) return;
    out::LoopSpec s;
    s.loopMs = spec->loop_ms;
    s.crossfadeMs = spec->crossfade_ms;
    s.fps = spec->fps;
    fill(s, spec);
}

int geode_loop_limits(int64_t* o, int capacity) {
    const int64_t limits[] = {out::LoopSpec::kMinLoopMs,      out::LoopSpec::kMaxLoopMs,
                              out::LoopSpec::kMinCrossfadeMs, out::LoopSpec::kMaxCrossfadeMs,
                              out::LoopSpec::kDefaultCrossfadeMs, out::kMaxDriftStops};
    const int count = static_cast<int>(sizeof limits / sizeof limits[0]);
    for (int i = 0; o && i < count && i < capacity; ++i) o[i] = limits[i];
    return count;
}

float geode_loop_seam_weight(int tail, int seam_frames) { return out::seamWeight(tail, seam_frames); }

int geode_loudness_window(int target, double* out4) {
    out::loudness::Window w;
    if (!out::loudness::windowOf(target, &w)) return 0;
    if (out4) {
        out4[0] = w.aimLufs;
        out4[1] = w.lowLufs;
        out4[2] = w.highLufs;
        out4[3] = w.ceilingDbtp;
    }
    return 1;
}

int geode_loudness_advise(int target, int gated, double lufs, double true_peak_dbtp, double* o) {
    const auto a = out::loudness::advise(target, gated != 0, lufs, true_peak_dbtp);
    if (o) {
        const double values[GEODE_LOUDNESS_ADVICE_DOUBLES] = {
            a.gainDb, a.gainForLevelDb, a.resultingLufs, a.resultingTruePeakDbtp, static_cast<double>(a.direction),
            a.ceilingLimited ? 1.0 : 0.0, static_cast<double>(a.band), a.quietLu, a.peakWarning ? 1.0 : 0.0};
        std::copy(values, values + GEODE_LOUDNESS_ADVICE_DOUBLES, o);
    }
    return a.kind;
}

int geode_loop_extend_plan(int64_t loop_us, int64_t total_us, int loops, int64_t* offsets, int32_t* loop_index,
                           int capacity) {
    const auto plan = out::LoopExtendPlan::of(loop_us, total_us, loops);
    const auto& writes = plan.writes();
    const int writable = std::min(plan.repeats(), std::max(capacity, 0));
    for (int i = 0; i < writable; ++i) {
        if (offsets) offsets[i] = writes[static_cast<size_t>(i)].offsetUs;
        if (loop_index) loop_index[i] = writes[static_cast<size_t>(i)].loopIndex;
    }
    return plan.repeats();
}

int64_t geode_loop_estimated_bytes(int64_t loop_us, int64_t total_us, int64_t loop_bytes, int64_t soundtrack_bytes) {
    return out::LoopExtendPlan::of(loop_us, total_us, 1).estimatedBytes(loop_bytes, soundtrack_bytes);
}

int geode_loop_fitting_bit_rate(int64_t total_us, int64_t limit_bytes, int64_t soundtrack_bytes) {
    return out::LoopExtendPlan::of(total_us, total_us, 1).fittingBitRate(limit_bytes, soundtrack_bytes);
}

int geode_export_capped_video_bps(int requested_bps, int64_t duration_us, int64_t audio_bytes, int64_t limit_bytes) {
    return out::encode::cappedVideoBps(requested_bps, duration_us, audio_bytes,
                                       limit_bytes > 0 ? limit_bytes : out::encode::kTargetBytes);
}

int64_t geode_export_muxer_hard_limit_bytes(void) { return out::encode::kMuxerHardLimitBytes; }

void geode_export_frame_size(int short_side, int w_ratio, int h_ratio, int* out_w, int* out_h) {
    const auto size = out::encode::frameSize(short_side, w_ratio, h_ratio);
    if (out_w) *out_w = size.width;
    if (out_h) *out_h = size.height;
}

int geode_export_clamp_fps(int requested_fps) { return out::encode::clampFps(requested_fps); }

int geode_record_max_length_count(void) { return out::record::kMaxLengthCount; }

int geode_record_max_length_at(int index) { return out::record::maxLengthAt(index); }

int geode_record_snap_max_length(int requested_sec) { return out::record::snapMaxLength(requested_sec); }

int64_t geode_record_estimate_bytes(int video_bps, int length_sec, int* out_lowered) {
    const auto estimate = out::record::estimate(video_bps, length_sec);
    if (out_lowered) *out_lowered = estimate.lowered ? 1 : 0;
    return estimate.bytes;
}

size_t geode_pcm_downmix_s16(const int16_t* in, size_t frames, int src_channels, int dst_channels, int16_t* o) {
    return geode::audio::downmixS16(in, frames, src_channels, dst_channels, o);
}

void geode_pcm_float_to_s16(const float* in, size_t samples, int16_t* o) { geode::audio::floatToS16(in, samples, o); }

int geode_aac_rate_for(int source_rate) { return geode::audio::aacRateFor(source_rate); }

int geode_mix_rate(void) { return geode::audio::kMixRateHz; }

int64_t geode_aac_end_us(int64_t last_pts_us, int sample_rate) { return geode::audio::aacEndUs(last_pts_us, sample_rate); }

int64_t geode_pcm_s16_bytes_to_us(int64_t bytes, int rate, int channels) {
    return geode::audio::s16BytesToUs(bytes, rate, channels);
}

int64_t geode_pcm_samples_before_us(int64_t us, int rate, int channels) {
    return geode::audio::samplesBeforeUs(us, rate, channels);
}

int64_t geode_pcm_trim_s16(int64_t start_us, int64_t buf_pts_us, int64_t bytes, int rate, int channels,
                           int64_t* out_pts_us) {
    const auto t = geode::audio::trimS16(start_us, buf_pts_us, bytes, rate, channels);
    if (out_pts_us) *out_pts_us = t.ptsUs;
    return t.skipBytes;
}

}  // extern "C"
