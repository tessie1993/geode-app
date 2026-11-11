// JNI marshalling for the export planning and clip maths (geode_api.h, "Export planning and clip maths").
// Only copies in and out: every rule lives in core/output, core/signal/timeline, viz/ClipGrade, util/CubeLut,
// audio/dsp/PcmConvert and audio/dsp/PcmTiming. Arrays move with Get/Set<Type>ArrayRegion (one copy, nothing
// pinned, no Release to forget; NDK "JNI tips", Region calls), and every New<Type>Array is checked before it
// is filled.
#include <jni.h>

#include <cstdint>
#include <vector>

#include "api/geode_api.h"

namespace {

// Field order of the long[] the loop-spec calls fill; GeodeNative.LOOP_SPEC_* documents the same order.
constexpr jsize kLoopSpecLongs = 7;

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

// Loudness advice: the kind, and its numbers in out (GEODE_LOUDNESS_ADVICE_DOUBLES).
JNIEXPORT jint JNICALL Java_dev_geode_engine_bridge_GeodeNative_loudnessAdvise(JNIEnv* env, jobject, jint target,
                                                                               jboolean gated, jdouble lufs,
                                                                               jdouble truePeakDbtp, jdoubleArray out) {
    double values[GEODE_LOUDNESS_ADVICE_DOUBLES] = {};
    const int kind = geode_loudness_advise(target, gated ? 1 : 0, lufs, truePeakDbtp, values);
    if (out && env->GetArrayLength(out) >= GEODE_LOUDNESS_ADVICE_DOUBLES) {
        env->SetDoubleArrayRegion(out, 0, GEODE_LOUDNESS_ADVICE_DOUBLES, values);
    }
    return kind;
}

JNIEXPORT jboolean JNICALL Java_dev_geode_engine_bridge_GeodeNative_loudnessWindow(JNIEnv* env, jobject, jint target,
                                                                                   jdoubleArray out) {
    double values[4] = {};
    if (!geode_loudness_window(target, values)) return JNI_FALSE;
    if (out && env->GetArrayLength(out) >= 4) env->SetDoubleArrayRegion(out, 0, 4, values);
    return JNI_TRUE;
}

// Time-of-day drift stops as (hue offset, warmth offset) pairs.
JNIEXPORT jfloatArray JNICALL Java_dev_geode_engine_bridge_GeodeNative_loopDriftStops(JNIEnv* env, jobject,
                                                                                      jfloat hueTurns, jfloat warmth,
                                                                                      jint stops) {
    const int count = geode_loop_drift_stops(hueTurns, warmth, stops, nullptr, nullptr, 0);
    std::vector<float> hue(static_cast<size_t>(count));
    std::vector<float> warm(static_cast<size_t>(count));
    const int written = geode_loop_drift_stops(hueTurns, warmth, stops, hue.data(), warm.data(), count);
    std::vector<jfloat> flat(static_cast<size_t>(written) * 2);
    for (int i = 0; i < written; ++i) {
        flat[static_cast<size_t>(i) * 2] = hue[static_cast<size_t>(i)];
        flat[static_cast<size_t>(i) * 2 + 1] = warm[static_cast<size_t>(i)];
    }
    jfloatArray result = env->NewFloatArray(static_cast<jsize>(flat.size()));
    if (result && !flat.empty()) env->SetFloatArrayRegion(result, 0, static_cast<jsize>(flat.size()), flat.data());
    return result;
}

JNIEXPORT void JNICALL Java_dev_geode_engine_bridge_GeodeNative_loopDriftApply(JNIEnv* env, jobject, jfloat colorShift,
                                                                               jfloat temperature, jfloat hueOffset,
                                                                               jfloat warmthOffset, jfloatArray out) {
    if (!out || env->GetArrayLength(out) < 2) return;
    float values[2] = {};
    geode_loop_drift_apply(colorShift, temperature, hueOffset, warmthOffset, values);
    env->SetFloatArrayRegion(out, 0, 2, values);
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

JNIEXPORT jint JNICALL Java_dev_geode_engine_bridge_GeodeNative_exportClampFps(JNIEnv*, jobject, jint requestedFps) {
    return geode_export_clamp_fps(requestedFps);
}

// A recorded take's limits (output/RecordPlan).

JNIEXPORT jintArray JNICALL Java_dev_geode_engine_bridge_GeodeNative_recordMaxLengths(JNIEnv* env, jobject) {
    const jint count = geode_record_max_length_count();
    jintArray out = env->NewIntArray(count);
    if (!out) return nullptr;
    for (jint i = 0; i < count; ++i) {
        const jint sec = geode_record_max_length_at(i);
        env->SetIntArrayRegion(out, i, 1, &sec);
    }
    return out;
}

JNIEXPORT jint JNICALL Java_dev_geode_engine_bridge_GeodeNative_recordSnapMaxLength(JNIEnv*, jobject, jint requestedSec) {
    return geode_record_snap_max_length(requestedSec);
}

JNIEXPORT jlong JNICALL Java_dev_geode_engine_bridge_GeodeNative_recordEstimateBytes(JNIEnv*, jobject, jint videoBps,
                                                                                      jint lengthSec) {
    return geode_record_estimate_bytes(videoBps, lengthSec, nullptr);
}

JNIEXPORT jboolean JNICALL Java_dev_geode_engine_bridge_GeodeNative_recordRateLowered(JNIEnv*, jobject, jint videoBps,
                                                                                       jint lengthSec) {
    int lowered = 0;
    geode_record_estimate_bytes(videoBps, lengthSec, &lowered);
    return lowered ? JNI_TRUE : JNI_FALSE;
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

// PCM timing (audio/dsp/PcmTiming).

JNIEXPORT jint JNICALL Java_dev_geode_engine_bridge_GeodeNative_aacRateFor(JNIEnv*, jobject, jint sourceRate) {
    return geode_aac_rate_for(sourceRate);
}

JNIEXPORT jlong JNICALL Java_dev_geode_engine_bridge_GeodeNative_aacEndUs(JNIEnv*, jobject, jlong lastPtsUs,
                                                                           jint sampleRate) {
    return geode_aac_end_us(lastPtsUs, sampleRate);
}

JNIEXPORT jlong JNICALL Java_dev_geode_engine_bridge_GeodeNative_pcmS16BytesToUs(JNIEnv*, jobject, jlong bytes,
                                                                                  jint rate, jint channels) {
    return geode_pcm_s16_bytes_to_us(bytes, rate, channels);
}

JNIEXPORT jlong JNICALL Java_dev_geode_engine_bridge_GeodeNative_pcmSamplesBeforeUs(JNIEnv*, jobject, jlong us,
                                                                                     jint rate, jint channels) {
    return geode_pcm_samples_before_us(us, rate, channels);
}

JNIEXPORT jlong JNICALL Java_dev_geode_engine_bridge_GeodeNative_pcmTrimS16(JNIEnv* env, jobject, jlong startUs,
                                                                             jlong bufferPtsUs, jlong bytes, jint rate,
                                                                             jint channels, jlongArray outPtsUs) {
    int64_t pts = 0;
    const int64_t skip = geode_pcm_trim_s16(startUs, bufferPtsUs, bytes, rate, channels, &pts);
    if (outPtsUs && env->GetArrayLength(outPtsUs) >= 1) {
        const jlong value = pts;
        env->SetLongArrayRegion(outPtsUs, 0, 1, &value);
    }
    return skip;
}

}  // extern "C"
