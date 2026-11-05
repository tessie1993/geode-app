// JNI marshalling for the export planning and clip maths (geode_api.h, "Export planning and clip maths").
// Only copies in and out: every rule lives in core/output, core/signal/timeline, viz/ClipGrade, util/CubeLut
// and audio/dsp/PcmConvert. Arrays move with Get/Set<Type>ArrayRegion (one copy, nothing pinned, no Release
// to forget; NDK "JNI tips", Region calls), and every New<Type>Array is checked before it is filled.
#include <jni.h>

#include <algorithm>
#include <cstdint>
#include <vector>

#include "api/geode_api.h"

namespace {

// Field order of the long[] the loop-spec calls fill; GeodeNative.LOOP_SPEC_* documents the same order.
constexpr jsize kLoopSpecLongs = 7;
// Size of the render-ETA state double[]; GeodeNative.ETA_STATE_DOUBLES is the same.
constexpr jsize kEtaStateDoubles = 9;
// Size of the render-plan long[]; GeodeNative.RENDER_PLAN_LONGS is the same.
constexpr jsize kRenderPlanLongs = 6;

void writeLoopSpec(JNIEnv* env, const geode_loop_spec& s, jlongArray out) {
    if (!out || env->GetArrayLength(out) < kLoopSpecLongs) return;
    const jlong values[kLoopSpecLongs] = {s.loop_ms,          s.crossfade_ms,     s.fps,
                                          s.loop_frames,      s.crossfade_frames, s.source_frames,
                                          s.loop_duration_us};
    env->SetLongArrayRegion(out, 0, kLoopSpecLongs, values);
}

std::vector<int64_t> longs(JNIEnv* env, jlongArray array) {
    const jsize n = array ? env->GetArrayLength(array) : 0;
    std::vector<int64_t> v(static_cast<size_t>(n));
    if (n > 0) env->GetLongArrayRegion(array, 0, n, reinterpret_cast<jlong*>(v.data()));
    return v;
}

std::vector<int32_t> ints(JNIEnv* env, jintArray array) {
    const jsize n = array ? env->GetArrayLength(array) : 0;
    std::vector<int32_t> v(static_cast<size_t>(n));
    if (n > 0) env->GetIntArrayRegion(array, 0, n, reinterpret_cast<jint*>(v.data()));
    return v;
}

// Chapter spans as one long[] of (start ms, duration ms, source) triples.
jlongArray chapterTriples(JNIEnv* env, int count, const std::vector<int64_t>& start, const std::vector<int64_t>& duration,
                          const std::vector<int32_t>& source) {
    std::vector<jlong> flat(static_cast<size_t>(count) * 3);
    for (int i = 0; i < count; ++i) {
        flat[static_cast<size_t>(i) * 3] = start[static_cast<size_t>(i)];
        flat[static_cast<size_t>(i) * 3 + 1] = duration[static_cast<size_t>(i)];
        flat[static_cast<size_t>(i) * 3 + 2] = source[static_cast<size_t>(i)];
    }
    jlongArray result = env->NewLongArray(static_cast<jsize>(flat.size()));
    if (result && !flat.empty()) env->SetLongArrayRegion(result, 0, static_cast<jsize>(flat.size()), flat.data());
    return result;
}

jintArray lutArray(JNIEnv* env, geode_cube_lut* lut) {
    if (!lut) return nullptr;
    const int size = geode_cube_lut_size(lut);
    const jsize entries = static_cast<jsize>(size) * size * size;
    jintArray result = env->NewIntArray(entries);
    if (result && entries > 0) {
        env->SetIntArrayRegion(result, 0, entries, reinterpret_cast<const jint*>(geode_cube_lut_argb(lut)));
    }
    geode_cube_lut_destroy(lut);
    return result;
}

}  // namespace

extern "C" {

// Frame clock (output/FrameClock).

JNIEXPORT jlong JNICALL Java_dev_geode_engine_bridge_GeodeNative_exportTotalFrames(JNIEnv*, jobject, jlong durationUs,
                                                                                    jint fps) {
    return geode_export_total_frames(durationUs, fps);
}

JNIEXPORT jlong JNICALL Java_dev_geode_engine_bridge_GeodeNative_exportFramePtsNs(JNIEnv*, jobject, jlong frame, jint fps) {
    return geode_export_frame_pts_ns(frame, fps);
}

JNIEXPORT jlong JNICALL Java_dev_geode_engine_bridge_GeodeNative_exportFrameTimeMs(JNIEnv*, jobject, jlong frame, jint fps) {
    return geode_export_frame_time_ms(frame, fps);
}

// Loops (output/LoopPlan).

JNIEXPORT void JNICALL Java_dev_geode_engine_bridge_GeodeNative_loopSpecOf(JNIEnv* env, jobject, jlong loopMs,
                                                                            jlong crossfadeMs, jint fps, jlongArray out) {
    geode_loop_spec s{};
    geode_loop_spec_of(loopMs, crossfadeMs, fps, &s);
    writeLoopSpec(env, s, out);
}

JNIEXPORT void JNICALL Java_dev_geode_engine_bridge_GeodeNative_loopSpecBudgeted(JNIEnv* env, jobject, jlong loopMs,
                                                                                  jlong crossfadeMs, jint fps, jint width,
                                                                                  jint height, jlongArray out) {
    geode_loop_spec in{};
    in.loop_ms = loopMs;
    in.crossfade_ms = crossfadeMs;
    in.fps = fps;
    geode_loop_spec s{};
    geode_loop_spec_budgeted(&in, width, height, &s);
    writeLoopSpec(env, s, out);
}

JNIEXPORT void JNICALL Java_dev_geode_engine_bridge_GeodeNative_loopSpecDerive(JNIEnv* env, jobject, jlong loopMs,
                                                                                jlong crossfadeMs, jint fps, jlongArray out) {
    geode_loop_spec s{};
    s.loop_ms = loopMs;
    s.crossfade_ms = crossfadeMs;
    s.fps = fps;
    geode_loop_spec_fill(&s);
    writeLoopSpec(env, s, out);
}

JNIEXPORT jfloat JNICALL Java_dev_geode_engine_bridge_GeodeNative_loopSeamWeight(JNIEnv*, jobject, jint tail,
                                                                                  jint seamFrames) {
    return geode_loop_seam_weight(tail, seamFrames);
}

JNIEXPORT jlongArray JNICALL Java_dev_geode_engine_bridge_GeodeNative_loopExtendPlan(JNIEnv* env, jobject, jlong loopUs,
                                                                                      jlong totalUs, jint loops) {
    const int repeats = geode_loop_extend_plan(loopUs, totalUs, loops, nullptr, nullptr, 0);
    std::vector<int64_t> offsets(static_cast<size_t>(repeats));
    std::vector<int32_t> index(static_cast<size_t>(repeats));
    geode_loop_extend_plan(loopUs, totalUs, loops, offsets.data(), index.data(), repeats);
    std::vector<jlong> flat(static_cast<size_t>(repeats) * 2);
    for (int i = 0; i < repeats; ++i) {
        flat[static_cast<size_t>(i) * 2] = offsets[static_cast<size_t>(i)];
        flat[static_cast<size_t>(i) * 2 + 1] = index[static_cast<size_t>(i)];
    }
    jlongArray result = env->NewLongArray(static_cast<jsize>(flat.size()));
    if (result && !flat.empty()) env->SetLongArrayRegion(result, 0, static_cast<jsize>(flat.size()), flat.data());
    return result;
}

JNIEXPORT jlong JNICALL Java_dev_geode_engine_bridge_GeodeNative_loopEstimatedBytes(JNIEnv*, jobject, jlong loopUs,
                                                                                     jlong totalUs, jlong loopBytes,
                                                                                     jlong soundtrackBytes) {
    return geode_loop_estimated_bytes(loopUs, totalUs, loopBytes, soundtrackBytes);
}

JNIEXPORT jint JNICALL Java_dev_geode_engine_bridge_GeodeNative_loopFittingBitRate(JNIEnv*, jobject, jlong totalUs,
                                                                                    jlong limitBytes, jlong soundtrackBytes) {
    return geode_loop_fitting_bit_rate(totalUs, limitBytes, soundtrackBytes);
}

// Encoder requests (output/EncodePlan).

JNIEXPORT jint JNICALL Java_dev_geode_engine_bridge_GeodeNative_exportCappedVideoBps(JNIEnv*, jobject, jint requestedBps,
                                                                                      jlong durationUs, jlong audioBytes,
                                                                                      jlong limitBytes) {
    return geode_export_capped_video_bps(requestedBps, durationUs, audioBytes, limitBytes);
}

JNIEXPORT jlong JNICALL Java_dev_geode_engine_bridge_GeodeNative_exportAudioBytes(JNIEnv*, jobject, jlong durationUs,
                                                                                   jint bps) {
    return geode_export_audio_bytes(durationUs, bps);
}

JNIEXPORT jlong JNICALL Java_dev_geode_engine_bridge_GeodeNative_exportMuxerHardLimitBytes(JNIEnv*, jobject) {
    return geode_export_muxer_hard_limit_bytes();
}

JNIEXPORT void JNICALL Java_dev_geode_engine_bridge_GeodeNative_exportFrameSize(JNIEnv* env, jobject, jint shortSide,
                                                                                 jint wRatio, jint hRatio, jintArray out) {
    if (!out || env->GetArrayLength(out) < 2) return;
    jint size[2] = {0, 0};
    geode_export_frame_size(shortSide, wRatio, hRatio, &size[0], &size[1]);
    env->SetIntArrayRegion(out, 0, 2, size);
}

JNIEXPORT jint JNICALL Java_dev_geode_engine_bridge_GeodeNative_exportFallbackFps(JNIEnv*, jobject, jint requestedFps) {
    return geode_export_fallback_fps(requestedFps);
}

// Render range (output/ExportRange). out gets (start ms, duration ms) when a part is returned.
JNIEXPORT jboolean JNICALL Java_dev_geode_engine_bridge_GeodeNative_exportRangeClamp(JNIEnv* env, jobject,
                                                                                      jlong startMs, jlong endMs,
                                                                                      jlong trackMs, jlongArray out) {
    int64_t start = 0;
    int64_t duration = 0;
    if (!geode_export_range_clamp(startMs, endMs, trackMs, &start, &duration)) return JNI_FALSE;
    if (out && env->GetArrayLength(out) >= 2) {
        const jlong values[2] = {start, duration};
        env->SetLongArrayRegion(out, 0, 2, values);
    }
    return JNI_TRUE;
}

JNIEXPORT jlong JNICALL Java_dev_geode_engine_bridge_GeodeNative_exportRangeMinMs(JNIEnv*, jobject) {
    return geode_export_range_min_ms();
}

// Project render plan (output/RenderPlan). Clips whose ints and longs disagree in count are cut to the shorter;
// out gets kRenderPlanLongs values in GeodeNative.RENDER_PLAN_* order when a plan is returned.
JNIEXPORT jboolean JNICALL Java_dev_geode_engine_bridge_GeodeNative_renderPlan(JNIEnv* env, jobject,
                                                                                jlongArray visualSpans,
                                                                                jintArray visualLanes,
                                                                                jlongArray audioSpans,
                                                                                jintArray audioLanes,
                                                                                jlong minDurationMs, jlongArray out) {
    const auto vSpans = longs(env, visualSpans);
    const auto vLanes = ints(env, visualLanes);
    const auto aSpans = longs(env, audioSpans);
    const auto aLanes = ints(env, audioLanes);
    const int vCount =
        static_cast<int>(std::min(vSpans.size() / GEODE_PLAN_CLIP_LONGS, vLanes.size() / GEODE_PLAN_CLIP_INTS));
    const int aCount =
        static_cast<int>(std::min(aSpans.size() / GEODE_PLAN_CLIP_LONGS, aLanes.size() / GEODE_PLAN_CLIP_INTS));
    geode_render_plan plan{};
    if (!geode_render_plan_of(vSpans.data(), vLanes.data(), vCount, aSpans.data(), aLanes.data(), aCount, minDurationMs,
                              &plan)) {
        return JNI_FALSE;
    }
    if (out && env->GetArrayLength(out) >= kRenderPlanLongs) {
        const jlong values[kRenderPlanLongs] = {plan.visual,        plan.song,          plan.programme_start_ms,
                                                plan.duration_ms,   plan.song_start_ms, plan.whole_song};
        env->SetLongArrayRegion(out, 0, kRenderPlanLongs, values);
    }
    return JNI_TRUE;
}

// Render ETA (output/RenderEta). The state is the caller's double[kEtaStateDoubles], in GeodeNative.ETA_*
// order; all zeros is a fresh estimate. Read in, stepped, written back.
JNIEXPORT jlong JNICALL Java_dev_geode_engine_bridge_GeodeNative_renderEtaSample(JNIEnv* env, jobject,
                                                                                 jdoubleArray state, jfloat progress,
                                                                                 jlong atMs, jboolean steady) {
    if (!state || env->GetArrayLength(state) < kEtaStateDoubles) return -1;
    jdouble v[kEtaStateDoubles];
    env->GetDoubleArrayRegion(state, 0, kEtaStateDoubles, v);
    geode_render_eta eta{};
    eta.steady = v[0] != 0.0 ? 1 : 0;
    eta.based = v[1] != 0.0 ? 1 : 0;
    eta.shown = v[2] != 0.0 ? 1 : 0;
    eta.base_progress = static_cast<float>(v[3]);
    eta.latest_progress = static_cast<float>(v[4]);
    eta.shown_seconds = static_cast<float>(v[5]);
    eta.steady_since_ms = static_cast<int64_t>(v[6]);
    eta.base_ms = static_cast<int64_t>(v[7]);
    eta.shown_at_ms = static_cast<int64_t>(v[8]);
    const int64_t left = geode_render_eta_sample(&eta, progress, atMs, steady ? 1 : 0);
    const jdouble back[kEtaStateDoubles] = {
        static_cast<jdouble>(eta.steady),          static_cast<jdouble>(eta.based),
        static_cast<jdouble>(eta.shown),           eta.base_progress,
        eta.latest_progress,                       eta.shown_seconds,
        static_cast<jdouble>(eta.steady_since_ms), static_cast<jdouble>(eta.base_ms),
        static_cast<jdouble>(eta.shown_at_ms)};
    env->SetDoubleArrayRegion(state, 0, kEtaStateDoubles, back);
    return left;
}

JNIEXPORT jint JNICALL Java_dev_geode_engine_bridge_GeodeNative_renderEtaPhrase(JNIEnv* env, jobject, jlong secondsLeft,
                                                                                jlongArray outCount) {
    int64_t count = 0;
    const int phrase = geode_render_eta_phrase(secondsLeft, &count);
    if (outCount && env->GetArrayLength(outCount) >= 1) {
        const jlong value = count;
        env->SetLongArrayRegion(outCount, 0, 1, &value);
    }
    return phrase;
}

// Caption cues (signal/timeline/Cues). Stateless: the triples and the origin pair live in Kotlin arrays.
JNIEXPORT jlongArray JNICALL Java_dev_geode_engine_bridge_GeodeNative_cuesForSpan(JNIEnv* env, jobject, jlongArray startsMs,
                                                                                  jlongArray endsMs, jlong spanStartMs,
                                                                                  jlong spanEndMs) {
    const auto starts = longs(env, startsMs);
    const auto ends = longs(env, endsMs);
    const int count = static_cast<int>(starts.size() < ends.size() ? starts.size() : ends.size());
    std::vector<int64_t> triples(static_cast<size_t>(count) * 3);
    const int n = geode_cues_for_span(starts.data(), ends.data(), count, spanStartMs, spanEndMs, triples.data(), count);
    jlongArray result = env->NewLongArray(static_cast<jsize>(n) * 3);
    if (result && n > 0) {
        env->SetLongArrayRegion(result, 0, static_cast<jsize>(n) * 3, reinterpret_cast<const jlong*>(triples.data()));
    }
    return result;
}

JNIEXPORT jint JNICALL Java_dev_geode_engine_bridge_GeodeNative_cuesAt(JNIEnv* env, jobject, jlongArray triples,
                                                                       jlongArray origin, jlong presentationUs) {
    const auto cues = longs(env, triples);
    if (!origin || env->GetArrayLength(origin) < 2) return -1;
    int64_t clock[2] = {0, 0};
    env->GetLongArrayRegion(origin, 0, 2, reinterpret_cast<jlong*>(clock));
    const int index = geode_cues_at(cues.data(), static_cast<int>(cues.size() / 3), clock, presentationUs);
    env->SetLongArrayRegion(origin, 0, 2, reinterpret_cast<const jlong*>(clock));
    return index;
}

// Chapters (output/Chapters).

JNIEXPORT jlongArray JNICALL Java_dev_geode_engine_bridge_GeodeNative_chaptersFromClips(JNIEnv* env, jobject,
                                                                                         jlongArray startsMs,
                                                                                         jlongArray durationsMs) {
    const auto starts = longs(env, startsMs);
    const auto durations = longs(env, durationsMs);
    const int count = static_cast<int>(starts.size() < durations.size() ? starts.size() : durations.size());
    std::vector<int64_t> s(static_cast<size_t>(count));
    std::vector<int64_t> d(static_cast<size_t>(count));
    std::vector<int32_t> src(static_cast<size_t>(count));
    const int n = geode_chapters_from_clips(starts.data(), durations.data(), count, s.data(), d.data(), src.data(), count);
    return chapterTriples(env, n, s, d, src);
}

JNIEXPORT jlongArray JNICALL Java_dev_geode_engine_bridge_GeodeNative_chaptersFromMarkers(JNIEnv* env, jobject,
                                                                                           jlongArray markersMs,
                                                                                           jlong totalMs) {
    const auto markers = longs(env, markersMs);
    const int count = static_cast<int>(markers.size());
    // A lead-in chapter may be added in front of the markers.
    std::vector<int64_t> s(static_cast<size_t>(count) + 1);
    std::vector<int64_t> d(static_cast<size_t>(count) + 1);
    std::vector<int32_t> src(static_cast<size_t>(count) + 1);
    const int n = geode_chapters_from_markers(markers.data(), count, totalMs, s.data(), d.data(), src.data(), count + 1);
    return chapterTriples(env, n, s, d, src);
}

// Clip speed ramps (signal/timeline/SpeedRamp). The keys arrive as parallel arrays: time, kind and interp per
// key, four value floats and four bezier handle floats per key. Returns [source span µs, segment starts…] and
// writes the segment speeds into speedsOut; null when there are no keys.
JNIEXPORT jlongArray JNICALL Java_dev_geode_engine_bridge_GeodeNative_speedRampBuild(
    JNIEnv* env, jobject, jlongArray keyTimesMs, jintArray keyKinds, jfloatArray keyValues, jintArray keyInterps,
    jfloatArray keyCurves, jlong clipStartMs, jlong clipDurationMs, jlong stepMs, jfloatArray speedsOut) {
    const auto times = longs(env, keyTimesMs);
    const jsize keys = static_cast<jsize>(times.size());
    if (keys == 0 || !keyKinds || !keyValues || !keyInterps || !keyCurves || !speedsOut ||
        env->GetArrayLength(keyKinds) < keys || env->GetArrayLength(keyInterps) < keys ||
        env->GetArrayLength(keyValues) < keys * 4 || env->GetArrayLength(keyCurves) < keys * 4) {
        return nullptr;
    }
    std::vector<jint> kinds(static_cast<size_t>(keys));
    std::vector<jint> interps(static_cast<size_t>(keys));
    std::vector<jfloat> values(static_cast<size_t>(keys) * 4);
    std::vector<jfloat> curves(static_cast<size_t>(keys) * 4);
    env->GetIntArrayRegion(keyKinds, 0, keys, kinds.data());
    env->GetIntArrayRegion(keyInterps, 0, keys, interps.data());
    env->GetFloatArrayRegion(keyValues, 0, keys * 4, values.data());
    env->GetFloatArrayRegion(keyCurves, 0, keys * 4, curves.data());

    geode_track* track = geode_track_create();
    for (jsize i = 0; i < keys; ++i) {
        const float* c = &curves[static_cast<size_t>(i) * 4];
        geode_track_add_key(track, times[static_cast<size_t>(i)], kinds[static_cast<size_t>(i)],
                            &values[static_cast<size_t>(i) * 4], interps[static_cast<size_t>(i)], c[0], c[1], c[2], c[3]);
    }
    geode_speed_ramp* ramp = geode_speed_ramp_create(track, clipStartMs, clipDurationMs, stepMs);
    geode_track_destroy(track);
    if (!ramp) return nullptr;

    const int segments = geode_speed_ramp_segments(ramp, nullptr, nullptr, 0);
    const jsize capacity = env->GetArrayLength(speedsOut);
    const int kept = segments < capacity ? segments : static_cast<int>(capacity);
    std::vector<jlong> out(static_cast<size_t>(kept) + 1);
    std::vector<jfloat> speeds(static_cast<size_t>(kept));
    out[0] = geode_speed_ramp_source_span_us(ramp);
    std::vector<int64_t> starts(static_cast<size_t>(kept));
    geode_speed_ramp_segments(ramp, starts.data(), speeds.data(), kept);
    geode_speed_ramp_destroy(ramp);
    for (int i = 0; i < kept; ++i) out[static_cast<size_t>(i) + 1] = starts[static_cast<size_t>(i)];

    jlongArray result = env->NewLongArray(static_cast<jsize>(out.size()));
    if (!result) return nullptr;
    env->SetLongArrayRegion(result, 0, static_cast<jsize>(out.size()), out.data());
    if (kept > 0) env->SetFloatArrayRegion(speedsOut, 0, kept, speeds.data());
    return result;
}

JNIEXPORT jfloat JNICALL Java_dev_geode_engine_bridge_GeodeNative_clipSpeedSanitize(JNIEnv*, jobject, jfloat speed) {
    return geode_clip_speed_sanitize(speed);
}

// Clip grade (viz/ClipGrade) and colour cubes (util/CubeLut).

JNIEXPORT void JNICALL Java_dev_geode_engine_bridge_GeodeNative_clipGradeMatrix(JNIEnv* env, jobject, jfloat brightness,
                                                                                 jfloat contrast, jfloat saturationPercent,
                                                                                 jfloat hueDegrees, jfloatArray out) {
    if (!out || env->GetArrayLength(out) < 16) return;
    float m[16];
    geode_clip_grade_matrix(brightness, contrast, saturationPercent, hueDegrees, m);
    env->SetFloatArrayRegion(out, 0, 16, m);
}

JNIEXPORT jintArray JNICALL Java_dev_geode_engine_bridge_GeodeNative_cubeLutParse(JNIEnv* env, jobject, jbyteArray text) {
    if (!text) return nullptr;
    const jsize n = env->GetArrayLength(text);
    std::vector<char> bytes(static_cast<size_t>(n));
    if (n > 0) env->GetByteArrayRegion(text, 0, n, reinterpret_cast<jbyte*>(bytes.data()));
    return lutArray(env, geode_cube_lut_parse(bytes.data(), bytes.size()));
}

JNIEXPORT jintArray JNICALL Java_dev_geode_engine_bridge_GeodeNative_cubeLutGamma(JNIEnv* env, jobject, jfloat red,
                                                                                   jfloat green, jfloat blue, jint size) {
    return lutArray(env, geode_cube_lut_gamma(red, green, blue, size));
}

// PCM conversions (audio/dsp/PcmConvert).

JNIEXPORT jshortArray JNICALL Java_dev_geode_engine_bridge_GeodeNative_pcmDownmixS16(JNIEnv* env, jobject,
                                                                                      jshortArray input, jint frames,
                                                                                      jint srcChannels, jint dstChannels) {
    if (!input || frames <= 0 || srcChannels <= 0 || dstChannels <= 0 ||
        static_cast<int64_t>(env->GetArrayLength(input)) < static_cast<int64_t>(frames) * srcChannels) {
        return env->NewShortArray(0);
    }
    std::vector<int16_t> in(static_cast<size_t>(frames) * static_cast<size_t>(srcChannels));
    env->GetShortArrayRegion(input, 0, static_cast<jsize>(in.size()), reinterpret_cast<jshort*>(in.data()));
    std::vector<int16_t> out(static_cast<size_t>(frames) * static_cast<size_t>(dstChannels));
    const size_t written = geode_pcm_downmix_s16(in.data(), static_cast<size_t>(frames), srcChannels, dstChannels, out.data());
    const auto samples = static_cast<jsize>(written * static_cast<size_t>(dstChannels));
    jshortArray result = env->NewShortArray(samples);
    if (result && samples > 0) env->SetShortArrayRegion(result, 0, samples, reinterpret_cast<const jshort*>(out.data()));
    return result;
}

JNIEXPORT jshortArray JNICALL Java_dev_geode_engine_bridge_GeodeNative_pcmFloatToS16(JNIEnv* env, jobject,
                                                                                      jfloatArray input, jint count) {
    if (!input || count <= 0 || env->GetArrayLength(input) < count) return env->NewShortArray(0);
    std::vector<float> in(static_cast<size_t>(count));
    env->GetFloatArrayRegion(input, 0, count, in.data());
    std::vector<int16_t> out(static_cast<size_t>(count));
    geode_pcm_float_to_s16(in.data(), in.size(), out.data());
    jshortArray result = env->NewShortArray(count);
    if (result) env->SetShortArrayRegion(result, 0, count, reinterpret_cast<const jshort*>(out.data()));
    return result;
}

JNIEXPORT void JNICALL Java_dev_geode_engine_bridge_GeodeNative_pcmS16ToFloat(JNIEnv* env, jobject, jshortArray input,
                                                                               jint count, jfloatArray out) {
    if (!input || !out || count <= 0 || env->GetArrayLength(input) < count || env->GetArrayLength(out) < count) return;
    std::vector<int16_t> in(static_cast<size_t>(count));
    env->GetShortArrayRegion(input, 0, count, reinterpret_cast<jshort*>(in.data()));
    std::vector<float> f(static_cast<size_t>(count));
    geode_pcm_s16_to_float(in.data(), in.size(), f.data());
    env->SetFloatArrayRegion(out, 0, count, f.data());
}

}  // extern "C"
