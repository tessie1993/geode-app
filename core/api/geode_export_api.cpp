// The C ABI over the export planning and clip maths (geode_api.h, "Export planning and clip maths"). Only
// argument checks and copies live here; the rules are in output/, signal/timeline/, viz/ClipGrade,
// util/CubeLut and audio/dsp/PcmConvert and PcmTiming, each host-tested in core/tests.
#include <algorithm>
#include <memory>
#include <string_view>
#include <utility>
#include <vector>

#include "api/geode_api.h"
#include "audio/dsp/PcmConvert.hpp"
#include "audio/dsp/PcmTiming.hpp"
#include "output/Chapters.hpp"
#include "output/EncodePlan.hpp"
#include "output/FrameClock.hpp"
#include "output/LoopPlan.hpp"
#include "signal/timeline/Keyframes.hpp"
#include "signal/timeline/SpeedRamp.hpp"
#include "util/CubeLut.hpp"
#include "viz/ClipGrade.hpp"

namespace tl = geode::signal::timeline;
namespace out = geode::output;

struct geode_track {
    tl::Track track;
};

struct geode_speed_ramp {
    tl::SpeedRamp ramp;
};

struct geode_cube_lut {
    geode::util::CubeLut lut;
};

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

int copyChapters(const std::vector<out::ChapterSpan>& spans, int64_t* start, int64_t* duration, int32_t* source,
                 int capacity) {
    const int n = static_cast<int>(spans.size());
    const int writable = std::min(n, std::max(capacity, 0));
    for (int i = 0; i < writable; ++i) {
        const auto& c = spans[static_cast<size_t>(i)];
        if (start) start[i] = c.startMs;
        if (duration) duration[i] = c.durationMs;
        if (source) source[i] = c.source;
    }
    return n;
}

tl::ValueKind kindOf(int kind) {
    return kind >= 0 && kind <= 4 ? static_cast<tl::ValueKind>(kind) : tl::ValueKind::Scalar;
}

}  // namespace

extern "C" {

int64_t geode_export_total_frames(int64_t duration_us, int fps) { return out::FrameClock(fps, 0).framesFor(duration_us); }

int64_t geode_export_frame_pts_ns(int64_t frame, int fps) { return out::FrameClock(fps, 0).ptsNs(frame); }

int64_t geode_export_frame_time_ms(int64_t frame, int fps) { return out::FrameClock(fps, 0).frameTimeMs(frame); }

void geode_loop_spec_of(int64_t loop_ms, int64_t crossfade_ms, int fps, geode_loop_spec* o) {
    if (o) fill(out::LoopSpec::of(loop_ms, crossfade_ms, fps), o);
}

void geode_loop_spec_budgeted(const geode_loop_spec* in, int width, int height, geode_loop_spec* o) {
    if (!in || !o) return;
    out::LoopSpec s;
    s.loopMs = in->loop_ms;
    s.crossfadeMs = in->crossfade_ms;
    s.fps = in->fps;
    fill(s.budgeted(width, height), o);
}

void geode_loop_spec_fill(geode_loop_spec* spec) {
    if (!spec) return;
    out::LoopSpec s;
    s.loopMs = spec->loop_ms;
    s.crossfadeMs = spec->crossfade_ms;
    s.fps = spec->fps;
    fill(s, spec);
}

float geode_loop_seam_weight(int tail, int seam_frames) { return out::seamWeight(tail, seam_frames); }

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

int64_t geode_export_audio_bytes(int64_t duration_us, int bps) { return out::encode::audioBytes(duration_us, bps); }

int64_t geode_export_muxer_hard_limit_bytes(void) { return out::encode::kMuxerHardLimitBytes; }

void geode_export_frame_size(int short_side, int w_ratio, int h_ratio, int* out_w, int* out_h) {
    const auto size = out::encode::frameSize(short_side, w_ratio, h_ratio);
    if (out_w) *out_w = size.width;
    if (out_h) *out_h = size.height;
}

int geode_export_fallback_fps(int requested_fps) { return out::encode::fallbackFps(requested_fps); }

int geode_chapters_from_clips(const int64_t* starts_ms, const int64_t* durations_ms, int count, int64_t* out_start_ms,
                              int64_t* out_duration_ms, int32_t* out_source, int capacity) {
    return copyChapters(out::chaptersFromClips(starts_ms, durations_ms, count), out_start_ms, out_duration_ms, out_source,
                        capacity);
}

int geode_chapters_from_markers(const int64_t* markers_ms, int count, int64_t total_ms, int64_t* out_start_ms,
                                int64_t* out_duration_ms, int32_t* out_source, int capacity) {
    return copyChapters(out::chaptersFromMarkers(markers_ms, count, total_ms), out_start_ms, out_duration_ms, out_source,
                        capacity);
}

geode_track* geode_track_create(void) { return new geode_track{}; }

void geode_track_destroy(geode_track* t) { std::unique_ptr<geode_track> owned(t); }

void geode_track_clear(geode_track* t) {
    if (t) t->track.clear();
}

void geode_track_add_key(geode_track* t, int64_t at_ms, int kind, const float* value4, int interp, float c1x, float c1y,
                         float c2x, float c2y) {
    if (!t) return;
    tl::Key key;
    key.atMs = at_ms;
    key.value.kind = kindOf(kind);
    if (value4) {
        for (int i = 0; i < 4; ++i) key.value.v[i] = value4[i];
    }
    switch (interp) {
        case 0: key.interp = tl::Interp::hold(); break;
        case 2: key.interp = tl::Interp::custom(tl::BezierCurve(c1x, c1y, c2x, c2y)); break;
        default: key.interp = tl::Interp::linear(); break;
    }
    t->track.add(key);
}

int geode_track_value_at(const geode_track* t, int64_t t_ms, int* out_kind, float* out_value4) {
    tl::Value v;
    if (!t || !t->track.valueAt(t_ms, &v)) return 0;
    if (out_kind) *out_kind = static_cast<int>(v.kind);
    if (out_value4) {
        for (int i = 0; i < 4; ++i) out_value4[i] = v.v[i];
    }
    return 1;
}

float geode_bezier_eval(float c1x, float c1y, float c2x, float c2y, float x) {
    return tl::BezierCurve(c1x, c1y, c2x, c2y).eval(x);
}

geode_speed_ramp* geode_speed_ramp_create(const geode_track* speed_track, int64_t clip_start_ms, int64_t clip_duration_ms,
                                          int64_t step_ms) {
    if (!speed_track || speed_track->track.empty()) return nullptr;
    return new geode_speed_ramp{tl::SpeedRamp::fromTrack(speed_track->track, clip_start_ms, clip_duration_ms,
                                                         step_ms > 0 ? step_ms : tl::SpeedRamp::kDefaultStepMs)};
}

void geode_speed_ramp_destroy(geode_speed_ramp* r) { std::unique_ptr<geode_speed_ramp> owned(r); }

float geode_speed_ramp_speed_at(const geode_speed_ramp* r, int64_t source_us) { return r ? r->ramp.speedAt(source_us) : 1.0f; }

int64_t geode_speed_ramp_next_change_us(const geode_speed_ramp* r, int64_t source_us) {
    return r ? r->ramp.nextChangeUs(source_us) : -1;
}

int64_t geode_speed_ramp_source_span_us(const geode_speed_ramp* r) { return r ? r->ramp.sourceSpanUs() : 0; }

int geode_speed_ramp_segments(const geode_speed_ramp* r, int64_t* out_starts_us, float* out_speeds, int capacity) {
    if (!r) return 0;
    const auto& starts = r->ramp.startsUs();
    const auto& speeds = r->ramp.speeds();
    const int n = static_cast<int>(starts.size());
    const int writable = std::min(n, std::max(capacity, 0));
    for (int i = 0; i < writable; ++i) {
        if (out_starts_us) out_starts_us[i] = starts[static_cast<size_t>(i)];
        if (out_speeds) out_speeds[i] = speeds[static_cast<size_t>(i)];
    }
    return n;
}

float geode_clip_speed_sanitize(float speed) { return tl::sanitizeClipSpeed(speed); }

void geode_clip_grade_matrix(float brightness, float contrast, float saturation_percent, float hue_degrees, float* out16) {
    if (out16) geode::viz::clipGradeMatrix(brightness, contrast, saturation_percent, hue_degrees, out16);
}

geode_cube_lut* geode_cube_lut_parse(const char* text, size_t length) {
    if (!text) return nullptr;
    auto lut = geode::util::CubeLut::parse(std::string_view(text, length));
    return lut ? new geode_cube_lut{std::move(*lut)} : nullptr;
}

geode_cube_lut* geode_cube_lut_gamma(float red, float green, float blue, int size) {
    return new geode_cube_lut{geode::util::CubeLut::gamma(red, green, blue, size)};
}

void geode_cube_lut_destroy(geode_cube_lut* l) { std::unique_ptr<geode_cube_lut> owned(l); }

int geode_cube_lut_size(const geode_cube_lut* l) { return l ? l->lut.size() : 0; }

const uint32_t* geode_cube_lut_argb(const geode_cube_lut* l) { return l ? l->lut.argb().data() : nullptr; }

size_t geode_pcm_downmix_s16(const int16_t* in, size_t frames, int src_channels, int dst_channels, int16_t* o) {
    return geode::audio::downmixS16(in, frames, src_channels, dst_channels, o);
}

void geode_pcm_float_to_s16(const float* in, size_t samples, int16_t* o) { geode::audio::floatToS16(in, samples, o); }


int geode_aac_rate_for(int source_rate) { return geode::audio::aacRateFor(source_rate); }

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
