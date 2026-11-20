// JNI for an export's platform audio work (core/api/geode_export_audio_api.h): marshalling only.
#include <jni.h>

#include <algorithm>
#include <cstdint>

#include "api/geode_export_audio_api.h"
#include "jni_util.hpp"

namespace {

// Layout of the double[] exportLoudnessMeasure fills; ExportAudioNative.exportLoudnessMeasure documents the same order.
constexpr jsize kReadingDoubles = 9;

geode_aac_job* aacJobOf(jlong handle) { return reinterpret_cast<geode_aac_job*>(handle); }

}  // namespace

extern "C" {

JNIEXPORT jint JNICALL Java_dev_geode_engine_bridge_ExportAudioNative_exportLoudnessMeasure(JNIEnv* env, jobject, jint fd,
                                                                                      jlong offset, jlong length,
                                                                                      jint target, jdoubleArray out) {
    geode_loudness_reading r{};
    geode_export_loudness_measure(fd, offset, length, target, &r);
    if (out && env->GetArrayLength(out) >= kReadingDoubles) {
        const jdouble values[kReadingDoubles] = {r.lufs,           r.true_peak_dbtp,
                                                 r.gain_db,        r.gain_for_level_db,
                                                 r.resulting_lufs, r.resulting_true_peak_dbtp,
                                                 r.ceiling_dbtp,   r.quiet_lu,
                                                 r.peak_warning ? 1.0 : 0.0};
        env->SetDoubleArrayRegion(out, 0, kReadingDoubles, values);
    }
    return r.sentence;
}

JNIEXPORT jlong JNICALL Java_dev_geode_engine_bridge_ExportAudioNative_aacJobCreate(JNIEnv*, jobject, jint fd, jlong offset,
                                                                             jlong length, jint outFd, jlong startMs,
                                                                             jlong maxDurationMs, jint outputRateHz) {
    return reinterpret_cast<jlong>(geode_aac_job_create(fd, offset, length, outFd, startMs, maxDurationMs, outputRateHz));
}

JNIEXPORT void JNICALL Java_dev_geode_engine_bridge_ExportAudioNative_aacJobDestroy(JNIEnv*, jobject, jlong job) {
    geode_aac_job_destroy(aacJobOf(job));
}

JNIEXPORT jboolean JNICALL Java_dev_geode_engine_bridge_ExportAudioNative_aacJobRun(JNIEnv*, jobject, jlong job,
                                                                           jint sliceMs) {
    return geode_aac_job_run(aacJobOf(job), sliceMs) != 0 ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jint JNICALL Java_dev_geode_engine_bridge_ExportAudioNative_aacJobState(JNIEnv*, jobject, jlong job) {
    return geode_aac_job_state(aacJobOf(job));
}

JNIEXPORT jint JNICALL Java_dev_geode_engine_bridge_ExportAudioNative_aacJobFailure(JNIEnv*, jobject, jlong job) {
    return geode_aac_job_failure(aacJobOf(job));
}

JNIEXPORT jstring JNICALL Java_dev_geode_engine_bridge_ExportAudioNative_aacJobDetail(JNIEnv* env, jobject, jlong job) {
    return geode::jni::utf8String(env, geode_aac_job_detail(aacJobOf(job)));
}

JNIEXPORT jfloat JNICALL Java_dev_geode_engine_bridge_ExportAudioNative_aacJobProgress(JNIEnv*, jobject, jlong job) {
    return geode_aac_job_progress(aacJobOf(job));
}

JNIEXPORT void JNICALL Java_dev_geode_engine_bridge_ExportAudioNative_aacJobTrack(JNIEnv* env, jobject, jlong job,
                                                                         jlongArray out) {
    int64_t values[GEODE_AAC_TRACK_VALUES] = {};
    geode_aac_job_track(aacJobOf(job), values);
    if (!out || env->GetArrayLength(out) < GEODE_AAC_TRACK_VALUES) return;
    jlong copy[GEODE_AAC_TRACK_VALUES];
    std::copy(values, values + GEODE_AAC_TRACK_VALUES, copy);
    env->SetLongArrayRegion(out, 0, GEODE_AAC_TRACK_VALUES, copy);
}

}
