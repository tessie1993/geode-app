// JNI marshalling for the export planning and PCM maths (geode_api.h, "Export planning and PCM maths").
// Only copies in and out: every rule lives in core/output, audio/dsp/PcmConvert and audio/dsp/PcmTiming. Arrays move with Get/Set<Type>ArrayRegion (one copy, nothing
// pinned, no Release to forget; NDK "JNI tips", Region calls), and every New<Type>Array is checked before it
// is filled.
#include <jni.h>

#include <algorithm>
#include <cstdint>
#include <vector>

#include "api/geode_api.h"
#include "api/geode_export_job_api.h"
#include "jni_util.hpp"

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

// An export's render (core/output/ExportFrame): handles and values only. `viz` is the renderer handle NativeViz
// holds; the job borrows it and is destroyed first. The descriptor is detached by the shell and owned natively.

JNIEXPORT jlong JNICALL Java_dev_geode_engine_bridge_GeodeNative_exportJobCreate(JNIEnv*, jobject, jlong viz, jint fd,
                                                                                jlong offset, jlong length, jint fps,
                                                                                jlong rangeStartMs) {
    return reinterpret_cast<jlong>(
        geode_export_job_create(reinterpret_cast<geode_viz*>(viz), fd, offset, length, fps, rangeStartMs));
}

JNIEXPORT void JNICALL Java_dev_geode_engine_bridge_GeodeNative_exportJobDestroy(JNIEnv*, jobject, jlong job) {
    geode_export_job_destroy(reinterpret_cast<geode_export_job*>(job));
}

JNIEXPORT jlong JNICALL Java_dev_geode_engine_bridge_GeodeNative_exportJobBegin(JNIEnv* env, jobject, jlong job,
                                                                               jint width, jint height, jstring sceneId,
                                                                               jstring milkPath, jboolean entitled,
                                                                               jlong pinDurationMs) {
    geode::jni::Utf8Chars scene(env, sceneId);
    geode::jni::Utf8Chars milk(env, milkPath);
    return geode_export_job_begin(reinterpret_cast<geode_export_job*>(job), width, height, scene.get(), milk.get(),
                                  entitled == JNI_TRUE ? 1 : 0, pinDurationMs);
}

JNIEXPORT jboolean JNICALL Java_dev_geode_engine_bridge_GeodeNative_exportJobWarm(JNIEnv*, jobject, jlong job) {
    return geode_export_job_warm(reinterpret_cast<geode_export_job*>(job)) ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jboolean JNICALL Java_dev_geode_engine_bridge_GeodeNative_exportJobFrame(JNIEnv*, jobject, jlong job,
                                                                                  jlong frame, jint targetFbo) {
    return geode_export_job_frame(reinterpret_cast<geode_export_job*>(job), frame, static_cast<uint32_t>(targetFbo))
               ? JNI_TRUE
               : JNI_FALSE;
}

// Loops (output/LoopPlan).

JNIEXPORT jlongArray JNICALL Java_dev_geode_engine_bridge_GeodeNative_loopLimits(JNIEnv* env, jobject) {
    int64_t limits[8];
    const int count = std::min(geode_loop_limits(limits, 8), 8);
    jlong values[8];
    for (int i = 0; i < count; ++i) values[i] = limits[i];
    jlongArray out = env->NewLongArray(count);
    if (out) env->SetLongArrayRegion(out, 0, count, values);
    return out;
}

JNIEXPORT void JNICALL Java_dev_geode_engine_bridge_GeodeNative_loopSpecOf(JNIEnv* env, jobject, jlong loopMs,
                                                                            jlong crossfadeMs, jint fps, jlongArray out) {
    geode_loop_spec s{};
    geode_loop_spec_of(loopMs, crossfadeMs, fps, &s);
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

// A render's time left (output/RenderEta).
JNIEXPORT jlong JNICALL Java_dev_geode_engine_bridge_GeodeNative_renderEtaCreate(JNIEnv*, jobject) {
    return reinterpret_cast<jlong>(geode_render_eta_create());
}

JNIEXPORT void JNICALL Java_dev_geode_engine_bridge_GeodeNative_renderEtaDestroy(JNIEnv*, jobject, jlong handle) {
    geode_render_eta_destroy(reinterpret_cast<geode_render_eta*>(handle));
}

JNIEXPORT jlong JNICALL Java_dev_geode_engine_bridge_GeodeNative_renderEtaSample(JNIEnv*, jobject, jlong handle,
                                                                                 jfloat progress, jlong atMs,
                                                                                 jboolean steady) {
    return geode_render_eta_sample(reinterpret_cast<geode_render_eta*>(handle), progress, atMs, steady ? 1 : 0);
}

JNIEXPORT void JNICALL Java_dev_geode_engine_bridge_GeodeNative_renderEtaReset(JNIEnv*, jobject, jlong handle) {
    geode_render_eta_reset(reinterpret_cast<geode_render_eta*>(handle));
}

JNIEXPORT jint JNICALL Java_dev_geode_engine_bridge_GeodeNative_renderEtaPhrase(JNIEnv*, jobject,
                                                                                jlong secondsRemaining) {
    return geode_render_eta_phrase(secondsRemaining);
}

JNIEXPORT jint JNICALL Java_dev_geode_engine_bridge_GeodeNative_renderEtaPhraseCount(JNIEnv*, jobject,
                                                                                     jlong secondsRemaining) {
    return geode_render_eta_phrase_count(secondsRemaining);
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
