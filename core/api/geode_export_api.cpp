// The C ABI over the export planning and clip maths (geode_api.h, "Export planning and clip maths"). Only
// argument checks and copies live here; the rules are in output/ (FrameClock, LoopPlan, EncodePlan, Chapters,
// ExportRange, RenderEta, RenderJob, RenderBatch), signal/timeline/ (Keyframes, SpeedRamp, Cues), viz/ClipGrade,
// util/CubeLut and audio/dsp/PcmConvert, each host-tested in core/tests.
#include <algorithm>
#include <memory>
#include <string_view>
#include <utility>
#include <vector>

#include "api/geode_api.h"
#include "audio/dsp/PcmConvert.hpp"
#include "output/Chapters.hpp"
#include "output/EncodePlan.hpp"
#include "output/ExportRange.hpp"
#include "output/FrameClock.hpp"
#include "output/LoopPlan.hpp"
#include "output/RenderEta.hpp"
#include "output/RenderBatch.hpp"
#include "output/RenderJob.hpp"
#include "signal/timeline/Cues.hpp"
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

int64_t geode_export_samples_through(int64_t frame, int fps, int sample_rate) {
    return out::FrameClock(fps, sample_rate).samplesThrough(frame);
}

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

void geode_pcm_s16_to_float(const int16_t* in, size_t samples, float* o) { geode::audio::s16ToFloat(in, samples, o); }


int geode_export_range_clamp(int64_t start_ms, int64_t end_ms, int64_t track_ms, int64_t* out_start_ms,
                             int64_t* out_duration_ms) {
    out::ExportRange r;
    if (!out::clampExportRange(start_ms, end_ms, track_ms, &r)) return 0;
    if (out_start_ms) *out_start_ms = r.startMs;
    if (out_duration_ms) *out_duration_ms = r.durationMs;
    return 1;
}

int64_t geode_export_range_min_ms(void) { return out::kMinExportRangeMs; }

int64_t geode_render_eta_sample(geode_render_eta* state, float progress, int64_t at_ms, int steady) {
    if (!state) return -1;
    out::RenderEtaState s;
    s.steady = state->steady != 0;
    s.steadySinceMs = state->steady_since_ms;
    s.based = state->based != 0;
    s.baseMs = state->base_ms;
    s.baseProgress = state->base_progress;
    s.latestProgress = state->latest_progress;
    s.shown = state->shown != 0;
    s.shownSeconds = state->shown_seconds;
    s.shownAtMs = state->shown_at_ms;
    const int64_t left = out::renderEtaSample(s, progress, at_ms, steady != 0);
    state->steady = s.steady ? 1 : 0;
    state->steady_since_ms = s.steadySinceMs;
    state->based = s.based ? 1 : 0;
    state->base_ms = s.baseMs;
    state->base_progress = s.baseProgress;
    state->latest_progress = s.latestProgress;
    state->shown = s.shown ? 1 : 0;
    state->shown_seconds = s.shownSeconds;
    state->shown_at_ms = s.shownAtMs;
    return left;
}

int geode_render_eta_phrase(int64_t seconds_left, int64_t* out_count) {
    return static_cast<int>(out::renderEtaPhrase(seconds_left, out_count));
}

namespace {

// Outcome codes from Kotlin or a test: anything unknown is a failure, so a bad code never reads as saved.
out::RenderOutcome outcomeOf(int code) {
    switch (code) {
        case static_cast<int>(out::RenderOutcome::Saved):
        case static_cast<int>(out::RenderOutcome::Failed):
        case static_cast<int>(out::RenderOutcome::Cancelled):
        case static_cast<int>(out::RenderOutcome::Locked):
        case static_cast<int>(out::RenderOutcome::NothingToRender):
            return static_cast<out::RenderOutcome>(code);
        case static_cast<int>(out::RenderOutcome::None):
            return out::RenderOutcome::None;
        default:
            return out::RenderOutcome::Failed;
    }
}

}  // namespace

int geode_render_job_begin(int kind) {
    if (kind < static_cast<int>(out::RenderKind::Render) || kind > static_cast<int>(out::RenderKind::Loop)) return 0;
    return out::RenderJob::process().begin(static_cast<out::RenderKind>(kind)) ? 1 : 0;
}

int geode_render_job_publish(float progress, int64_t at_ms, int steady) {
    return out::RenderJob::process().publish(progress, at_ms, steady != 0) ? 1 : 0;
}

int geode_render_job_request_cancel(void) { return out::RenderJob::process().requestCancel() ? 1 : 0; }

int geode_render_job_cancel_requested(void) { return out::RenderJob::process().cancelRequested() ? 1 : 0; }

int geode_render_job_finish(int outcome) {
    return static_cast<int>(out::RenderJob::process().finish(outcomeOf(outcome)));
}

void geode_render_job_reset(void) { out::RenderJob::process().reset(); }

void geode_render_job_snapshot(geode_render_job_state* out_state) {
    if (!out_state) return;
    const out::RenderJobSnapshot s = out::RenderJob::process().snapshot();
    out_state->phase = static_cast<int32_t>(s.phase);
    out_state->kind = static_cast<int32_t>(s.kind);
    out_state->generation = s.generation;
    out_state->has_progress = s.hasProgress ? 1 : 0;
    out_state->progress = s.progress;
    out_state->seconds_left = s.secondsLeft;
    out_state->cancel_requested = s.cancelRequested ? 1 : 0;
    out_state->outcome = static_cast<int32_t>(s.outcome);
}

int geode_render_job_report(int outcome) { return static_cast<int>(out::reportFor(outcomeOf(outcome))); }

int geode_render_job_keeps_output(int outcome) { return out::keepsOutput(outcomeOf(outcome)) ? 1 : 0; }

int geode_render_batch_tracks(int scope, const char* const* keys, const int32_t* sources, int count,
                              int32_t* out_indices, int cap) {
    if (!keys || !sources || count <= 0) return 0;
    std::vector<std::string_view> views(static_cast<size_t>(count));
    for (int i = 0; i < count; ++i) {
        const char* key = keys[i];
        views[static_cast<size_t>(i)] = key ? std::string_view(key) : std::string_view();
    }
    return out::batchTracks(static_cast<out::BatchScope>(scope), views.data(), sources, count, out_indices,
                            std::max(cap, 0));
}

int geode_render_batch_next(int total, int done, int cancel_requested) {
    return out::batchNext(total, done, cancel_requested != 0);
}

int geode_render_batch_record(int32_t* codes, int total, int done, int outcome) {
    return out::batchRecord(codes, total, done, outcomeOf(outcome));
}

float geode_render_batch_progress(int total, int done, float within) { return out::batchProgress(total, done, within); }

int geode_render_batch_summarize(const int32_t* codes, int total, int done, geode_render_batch_summary* out_summary,
                                 int32_t* out_failed, int cap) {
    if (out_summary) {
        const out::BatchSummary s = out::summarizeBatch(codes, total, done);
        out_summary->total = s.total;
        out_summary->done = s.done;
        out_summary->saved = s.saved;
        out_summary->failed = s.failed;
        out_summary->cancelled = s.cancelled;
        out_summary->current = s.current;
        out_summary->last_saved = s.lastSaved;
        out_summary->first_failed = s.firstFailed;
        out_summary->report = static_cast<int32_t>(s.report);
        out_summary->finished = s.finished ? 1 : 0;
    }
    return out::batchFailed(codes, std::clamp(done, 0, std::max(total, 0)), out_failed, std::max(cap, 0));
}

int geode_cues_for_span(const int64_t* starts_ms, const int64_t* ends_ms, int count, int64_t span_start_ms,
                        int64_t span_end_ms, int64_t* out_triples, int cap) {
    const auto table = tl::CueTable::forSpan(starts_ms, ends_ms, count, span_start_ms, span_end_ms);
    const int n = std::min(static_cast<int>(table.cues().size()), out_triples ? std::max(cap, 0) : 0);
    for (int i = 0; i < n; ++i) {
        const auto& c = table.cues()[static_cast<size_t>(i)];
        out_triples[i * 3] = c.startMs;
        out_triples[i * 3 + 1] = c.endMs;
        out_triples[i * 3 + 2] = c.source;
    }
    return n;
}

int geode_cues_at(const int64_t* triples, int count, int64_t* origin2, int64_t presentation_us) {
    if (!triples || !origin2 || count <= 0) return -1;
    tl::CueClock clock{origin2[0], origin2[1]};
    const int64_t atMs = clock.itemMs(presentation_us);
    origin2[0] = clock.hasOrigin;
    origin2[1] = clock.originUs;
    return tl::CueTable::atTriples(triples, count, atMs);
}

}  // extern "C"
