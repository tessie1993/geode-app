// JNI for an export's platform audio work (core/api/geode_export_audio_api.h): marshalling only.
#include <jni.h>

#include <algorithm>
#include <cstdint>

#include "api/geode_export_audio_api.h"
#include "jni_util.hpp"

namespace {

// Layout of the double[] exportLoudnessMeasure fills; ExportAudioNative.exportLoudnessMeasure documents the same order.
constexpr jsize kReadingDoubles = 9;
// Sample-table rows copied per call into the long[] aacJobSamples returns.
constexpr int kRowsPerCopy = 256;

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

JNIEXPORT jlongArray JNICALL Java_dev_geode_engine_bridge_ExportAudioNative_aacJobSamples(JNIEnv* env, jobject, jlong job) {
    int64_t track[GEODE_AAC_TRACK_VALUES] = {};
    geode_aac_job_track(aacJobOf(job), track);
    const int64_t rows = track[GEODE_AAC_TRACK_SAMPLES];
    const int64_t longs = rows * GEODE_AAC_SAMPLE_VALUES;
    if (longs > INT32_MAX) return nullptr;
    jlongArray out = env->NewLongArray(static_cast<jsize>(longs));
    if (!out) return nullptr;
    int64_t chunk[kRowsPerCopy * GEODE_AAC_SAMPLE_VALUES];
    jlong copy[kRowsPerCopy * GEODE_AAC_SAMPLE_VALUES];
    for (int first = 0; first < rows;) {
        const int n = geode_aac_job_samples(aacJobOf(job), first, kRowsPerCopy, chunk);
        if (n <= 0) break;
        std::copy(chunk, chunk + n * GEODE_AAC_SAMPLE_VALUES, copy);
        env->SetLongArrayRegion(out, first * GEODE_AAC_SAMPLE_VALUES, n * GEODE_AAC_SAMPLE_VALUES, copy);
        first += n;
    }
    return out;
}

JNIEXPORT jbyteArray JNICALL Java_dev_geode_engine_bridge_ExportAudioNative_aacJobCsd(JNIEnv* env, jobject, jlong job) {
    int size = 0;
    const uint8_t* csd = geode_aac_job_csd(aacJobOf(job), &size);
    if (!csd || size <= 0) return nullptr;
    jbyteArray out = env->NewByteArray(size);
    if (out) env->SetByteArrayRegion(out, 0, size, reinterpret_cast<const jbyte*>(csd));
    return out;
}

}
