// JNI marshalling for the export planning and PCM maths (geode_api.h, "Export planning and PCM maths").
// Only copies in and out: every rule lives in core/output, audio/dsp/PcmConvert and audio/dsp/PcmTiming. Arrays move with Get/Set<Type>ArrayRegion (one copy, nothing
// pinned, no Release to forget; NDK "JNI tips", Region calls), and every New<Type>Array is checked before it
// is filled.
#include <jni.h>

#include <algorithm>
#include <cstdint>
#include <vector>

#include "api/geode_api.h"
#include "jni_util.hpp"

namespace {

// Field order of the long[] the loop-spec calls fill; ExportNative.LOOP_SPEC_* documents the same order.
constexpr jsize kLoopSpecLongs = 7;

void writeLoopSpec(JNIEnv* env, const geode_loop_spec& s, jlongArray out) {
    if (!out || env->GetArrayLength(out) < kLoopSpecLongs) return;
    const jlong values[kLoopSpecLongs] = {s.loop_ms,          s.crossfade_ms,     s.fps,
                                          s.loop_frames,      s.crossfade_frames, s.source_frames,
                                          s.loop_duration_us};
    env->SetLongArrayRegion(out, 0, kLoopSpecLongs, values);
}

}  // namespace

extern "C" {

// Loops (output/LoopPlan).

JNIEXPORT jlongArray JNICALL Java_dev_geode_engine_bridge_ExportNative_loopLimits(JNIEnv* env, jobject) {
    int64_t limits[8];
    const int count = std::min(geode_loop_limits(limits, 8), 8);
    jlong values[8];
    for (int i = 0; i < count; ++i) values[i] = limits[i];
    jlongArray out = env->NewLongArray(count);
    if (out) env->SetLongArrayRegion(out, 0, count, values);
    return out;
}

JNIEXPORT void JNICALL Java_dev_geode_engine_bridge_ExportNative_loopSpecOf(JNIEnv* env, jobject, jlong loopMs,
                                                                            jlong crossfadeMs, jint fps, jlongArray out) {
    geode_loop_spec s{};
    geode_loop_spec_of(loopMs, crossfadeMs, fps, &s);
    writeLoopSpec(env, s, out);
}

JNIEXPORT void JNICALL Java_dev_geode_engine_bridge_ExportNative_loopSpecDerive(JNIEnv* env, jobject, jlong loopMs,
                                                                                jlong crossfadeMs, jint fps, jlongArray out) {
    geode_loop_spec s{};
    s.loop_ms = loopMs;
    s.crossfade_ms = crossfadeMs;
    s.fps = fps;
    geode_loop_spec_fill(&s);
    writeLoopSpec(env, s, out);
}

JNIEXPORT jlongArray JNICALL Java_dev_geode_engine_bridge_ExportNative_loopExtendPlan(JNIEnv* env, jobject, jlong loopUs,
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

JNIEXPORT jlong JNICALL Java_dev_geode_engine_bridge_ExportNative_loopEstimatedBytes(JNIEnv*, jobject, jlong loopUs,
                                                                                     jlong totalUs, jlong loopBytes,
                                                                                     jlong soundtrackBytes) {
    return geode_loop_estimated_bytes(loopUs, totalUs, loopBytes, soundtrackBytes);
}

JNIEXPORT jint JNICALL Java_dev_geode_engine_bridge_ExportNative_loopFittingBitRate(JNIEnv*, jobject, jlong totalUs,
                                                                                    jlong limitBytes, jlong soundtrackBytes) {
    return geode_loop_fitting_bit_rate(totalUs, limitBytes, soundtrackBytes);
}

// Loop extend's copy decisions (output/LoopExtend): marshalling only.
static_assert(sizeof(jlong) == sizeof(int64_t) && sizeof(jint) == sizeof(int), "the arrays move as they are");

JNIEXPORT jint JNICALL Java_dev_geode_engine_bridge_ExportNative_loopRepeatSamples(JNIEnv* env, jobject, jlongArray times,
                                                                                   jintArray flags, jlong offsetUs,
                                                                                   jlong limitUs, jlongArray outPts,
                                                                                   jintArray outFlags) {
    const jsize count = std::min({env->GetArrayLength(times), env->GetArrayLength(flags), env->GetArrayLength(outPts),
                                  env->GetArrayLength(outFlags)});
    std::vector<int64_t> t(static_cast<size_t>(count));
    std::vector<int> f(static_cast<size_t>(count));
    env->GetLongArrayRegion(times, 0, count, reinterpret_cast<jlong*>(t.data()));
    env->GetIntArrayRegion(flags, 0, count, f.data());
    std::vector<int64_t> pts(static_cast<size_t>(count));
    std::vector<int> outF(static_cast<size_t>(count));
    const int n = geode_loop_repeat_samples(t.data(), f.data(), count, offsetUs, limitUs, pts.data(), outF.data());
    if (n > 0) {
        env->SetLongArrayRegion(outPts, 0, n, reinterpret_cast<const jlong*>(pts.data()));
        env->SetIntArrayRegion(outFlags, 0, n, outF.data());
    }
    return n;
}

JNIEXPORT void JNICALL Java_dev_geode_engine_bridge_ExportNative_loopClipStarts(JNIEnv* env, jobject, jlongArray durations,
                                                                                jlongArray out) {
    const jsize clips = std::min(env->GetArrayLength(durations), env->GetArrayLength(out) - 1);
    if (clips < 0) return;
    std::vector<int64_t> d(static_cast<size_t>(clips));
    if (clips > 0) env->GetLongArrayRegion(durations, 0, clips, reinterpret_cast<jlong*>(d.data()));
    std::vector<int64_t> starts(static_cast<size_t>(clips) + 1);
    geode_loop_clip_starts(d.data(), clips, starts.data());
    env->SetLongArrayRegion(out, 0, clips + 1, reinterpret_cast<const jlong*>(starts.data()));
}

// The sample times of the whole soundtrack are pinned, not copied: a ten-hour mix is millions of entries and this
// runs once per repeat.
JNIEXPORT jint JNICALL Java_dev_geode_engine_bridge_ExportNative_loopAudioDue(JNIEnv* env, jobject, jlongArray clipStarts,
                                                                              jintArray sampleCounts, jlongArray samplePts,
                                                                              jintArray cursor, jlong untilUs,
                                                                              jintArray outClip, jintArray outSample,
                                                                              jlongArray outPts) {
    const jsize clips = std::min(env->GetArrayLength(clipStarts), env->GetArrayLength(sampleCounts));
    const jsize capacity = std::min({env->GetArrayLength(outClip), env->GetArrayLength(outSample), env->GetArrayLength(outPts)});
    if (env->GetArrayLength(cursor) < 2) return 0;
    std::vector<int64_t> starts(static_cast<size_t>(clips));
    std::vector<int> counts(static_cast<size_t>(clips));
    if (clips > 0) {
        env->GetLongArrayRegion(clipStarts, 0, clips, reinterpret_cast<jlong*>(starts.data()));
        env->GetIntArrayRegion(sampleCounts, 0, clips, counts.data());
    }
    int c[2] = {};
    env->GetIntArrayRegion(cursor, 0, 2, reinterpret_cast<jint*>(c));
    std::vector<int> clipOut(static_cast<size_t>(capacity));
    std::vector<int> sampleOut(static_cast<size_t>(capacity));
    std::vector<int64_t> ptsOut(static_cast<size_t>(capacity));
    auto* pts = static_cast<const int64_t*>(env->GetPrimitiveArrayCritical(samplePts, nullptr));
    if (!pts) return 0;
    const int n = geode_loop_audio_due(starts.data(), counts.data(), clips, pts, c, untilUs, clipOut.data(),
                                       sampleOut.data(), ptsOut.data(), capacity);
    env->ReleasePrimitiveArrayCritical(samplePts, const_cast<int64_t*>(pts), JNI_ABORT);
    env->SetIntArrayRegion(cursor, 0, 2, reinterpret_cast<const jint*>(c));
    if (n > 0) {
        env->SetIntArrayRegion(outClip, 0, n, clipOut.data());
        env->SetIntArrayRegion(outSample, 0, n, sampleOut.data());
        env->SetLongArrayRegion(outPts, 0, n, reinterpret_cast<const jlong*>(ptsOut.data()));
    }
    return n;
}

JNIEXPORT jfloat JNICALL Java_dev_geode_engine_bridge_ExportNative_loopCopyProgress(JNIEnv*, jobject, jint done,
                                                                                    jint repeats) {
    return geode_loop_copy_progress(done, repeats);
}

JNIEXPORT jfloat JNICALL Java_dev_geode_engine_bridge_ExportNative_loopTranscodeProgress(JNIEnv*, jobject, jint index,
                                                                                         jfloat clipProgress, jint clips) {
    return geode_loop_transcode_progress(index, clipProgress, clips);
}

JNIEXPORT jint JNICALL Java_dev_geode_engine_bridge_ExportNative_loopSampleBufferBytes(JNIEnv*, jobject, jint declaredBytes,
                                                                                       jint width, jint height) {
    return geode_loop_sample_buffer_bytes(declaredBytes, width, height);
}

// Encoder requests (output/EncodePlan).

JNIEXPORT jlong JNICALL Java_dev_geode_engine_bridge_ExportNative_exportMuxerHardLimitBytes(JNIEnv*, jobject) {
    return geode_export_muxer_hard_limit_bytes();
}

JNIEXPORT void JNICALL Java_dev_geode_engine_bridge_ExportNative_exportFrameSize(JNIEnv* env, jobject, jint shortSide,
                                                                                 jint wRatio, jint hRatio, jintArray out) {
    if (!out || env->GetArrayLength(out) < 2) return;
    jint size[2] = {0, 0};
    geode_export_frame_size(shortSide, wRatio, hRatio, &size[0], &size[1]);
    env->SetIntArrayRegion(out, 0, 2, size);
}

// A render's time left (output/RenderEta).
JNIEXPORT jlong JNICALL Java_dev_geode_engine_bridge_ExportNative_renderEtaCreate(JNIEnv*, jobject) {
    return reinterpret_cast<jlong>(geode_render_eta_create());
}

JNIEXPORT void JNICALL Java_dev_geode_engine_bridge_ExportNative_renderEtaDestroy(JNIEnv*, jobject, jlong handle) {
    geode_render_eta_destroy(reinterpret_cast<geode_render_eta*>(handle));
}

JNIEXPORT jlong JNICALL Java_dev_geode_engine_bridge_ExportNative_renderEtaSample(JNIEnv*, jobject, jlong handle,
                                                                                 jfloat progress, jlong atMs,
                                                                                 jboolean steady) {
    return geode_render_eta_sample(reinterpret_cast<geode_render_eta*>(handle), progress, atMs, steady ? 1 : 0);
}

JNIEXPORT void JNICALL Java_dev_geode_engine_bridge_ExportNative_renderEtaReset(JNIEnv*, jobject, jlong handle) {
    geode_render_eta_reset(reinterpret_cast<geode_render_eta*>(handle));
}

JNIEXPORT jint JNICALL Java_dev_geode_engine_bridge_ExportNative_renderEtaPhrase(JNIEnv*, jobject,
                                                                                jlong secondsRemaining) {
    return geode_render_eta_phrase(secondsRemaining);
}

JNIEXPORT jint JNICALL Java_dev_geode_engine_bridge_ExportNative_renderEtaPhraseCount(JNIEnv*, jobject,
                                                                                     jlong secondsRemaining) {
    return geode_render_eta_phrase_count(secondsRemaining);
}

// A recorded take's limits (output/RecordPlan).

JNIEXPORT jintArray JNICALL Java_dev_geode_engine_bridge_ExportNative_recordMaxLengths(JNIEnv* env, jobject) {
    const jint count = geode_record_max_length_count();
    jintArray out = env->NewIntArray(count);
    if (!out) return nullptr;
    for (jint i = 0; i < count; ++i) {
        const jint sec = geode_record_max_length_at(i);
        env->SetIntArrayRegion(out, i, 1, &sec);
    }
    return out;
}

JNIEXPORT jint JNICALL Java_dev_geode_engine_bridge_ExportNative_recordSnapMaxLength(JNIEnv*, jobject, jint requestedSec) {
    return geode_record_snap_max_length(requestedSec);
}

JNIEXPORT jlong JNICALL Java_dev_geode_engine_bridge_ExportNative_recordEstimateBytes(JNIEnv*, jobject, jint videoBps,
                                                                                      jint lengthSec) {
    return geode_record_estimate_bytes(videoBps, lengthSec, nullptr);
}

JNIEXPORT jboolean JNICALL Java_dev_geode_engine_bridge_ExportNative_recordRateLowered(JNIEnv*, jobject, jint videoBps,
                                                                                       jint lengthSec) {
    int lowered = 0;
    geode_record_estimate_bytes(videoBps, lengthSec, &lowered);
    return lowered ? JNI_TRUE : JNI_FALSE;
}

// The one rate a mix's clips are brought to (audio/dsp/PcmTiming).

JNIEXPORT jint JNICALL Java_dev_geode_engine_bridge_ExportNative_mixRateHz(JNIEnv*, jobject) {
    return geode_mix_rate();
}

}  // extern "C"
