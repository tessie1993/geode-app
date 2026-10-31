#include <jni.h>

#include <vector>

#include "api/geode_api.h"
#include "jni_util.hpp"

namespace {

geode_resampler* resamplerOf(jlong handle) { return reinterpret_cast<geode_resampler*>(handle); }
geode_loudness* loudnessOf(jlong handle) { return reinterpret_cast<geode_loudness*>(handle); }

// Layout of the double[] loudnessFinish fills; GeodeNative.loudnessFinish documents the same order.
constexpr jsize kReportDoubles = 5;

}  // namespace

extern "C" {

JNIEXPORT jlong JNICALL
Java_dev_geode_engine_bridge_GeodeNative_resamplerCreate(JNIEnv*, jobject, jint inRate, jint outRate, jint channels) {
    return reinterpret_cast<jlong>(geode_resampler_create(inRate, outRate, channels));
}

JNIEXPORT void JNICALL
Java_dev_geode_engine_bridge_GeodeNative_resamplerDestroy(JNIEnv*, jobject, jlong handle) {
    geode_resampler_destroy(resamplerOf(handle));
}

JNIEXPORT jshortArray JNICALL
Java_dev_geode_engine_bridge_GeodeNative_resamplerProcess(JNIEnv* env, jobject, jlong handle, jshortArray input,
                                                         jint frames, jint channels) {
    auto* r = resamplerOf(handle);
    if (!r || !input || frames <= 0 || channels <= 0 || env->GetArrayLength(input) < frames * channels) {
        return env->NewShortArray(0);
    }
    std::vector<int16_t> in(static_cast<size_t>(frames) * channels);
    env->GetShortArrayRegion(input, 0, static_cast<jsize>(in.size()), reinterpret_cast<jshort*>(in.data()));
    const size_t capacity = geode_resampler_max_out_frames(r, static_cast<size_t>(frames));
    std::vector<int16_t> out(capacity * channels);
    const int64_t written = geode_resampler_process(r, in.data(), static_cast<size_t>(frames), out.data(), capacity);
    const auto samples = static_cast<jsize>(written > 0 ? written * channels : 0);
    jshortArray result = env->NewShortArray(samples);
    if (result && samples > 0) env->SetShortArrayRegion(result, 0, samples, reinterpret_cast<const jshort*>(out.data()));
    return result;
}

JNIEXPORT jboolean JNICALL
Java_dev_geode_engine_bridge_GeodeNative_loudnessSupports(JNIEnv*, jobject, jint channels) {
    return geode_loudness_supports(channels) ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jlong JNICALL
Java_dev_geode_engine_bridge_GeodeNative_loudnessCreate(JNIEnv*, jobject, jint sampleRate, jint channels) {
    return reinterpret_cast<jlong>(geode_loudness_create(sampleRate, channels));
}

JNIEXPORT void JNICALL
Java_dev_geode_engine_bridge_GeodeNative_loudnessDestroy(JNIEnv*, jobject, jlong handle) {
    geode_loudness_destroy(loudnessOf(handle));
}

JNIEXPORT void JNICALL
Java_dev_geode_engine_bridge_GeodeNative_loudnessFeed(JNIEnv* env, jobject, jlong handle, jfloatArray samples, jint count) {
    geode::jni::FloatElements view(env, samples);
    if (!view.get() || count <= 0) return;
    const jsize n = count < view.length() ? count : view.length();
    geode_loudness_feed(loudnessOf(handle), view.get(), static_cast<size_t>(n));
}

JNIEXPORT jboolean JNICALL
Java_dev_geode_engine_bridge_GeodeNative_loudnessHasBlock(JNIEnv*, jobject, jlong handle) {
    return geode_loudness_has_block(loudnessOf(handle)) ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jboolean JNICALL
Java_dev_geode_engine_bridge_GeodeNative_loudnessFinish(JNIEnv* env, jobject, jlong handle, jdoubleArray out) {
    geode_loudness_report report;
    geode_loudness_finish(loudnessOf(handle), &report);
    if (out && env->GetArrayLength(out) >= kReportDoubles) {
        const jdouble values[kReportDoubles] = {report.integrated_lufs, report.true_peak_dbtp, report.sample_peak_dbfs,
                                                static_cast<jdouble>(report.measured_ms),
                                                static_cast<jdouble>(report.gated_block_count)};
        env->SetDoubleArrayRegion(out, 0, kReportDoubles, values);
    }
    return report.has_integrated ? JNI_TRUE : JNI_FALSE;
}

}
