#include <jni.h>

#include <array>
#include <vector>

#include "api/geode_api.h"
#include "jni_util.hpp"

namespace {

using geode::jni::FloatElements;

geode_analysis* analysisOf(jlong handle) { return reinterpret_cast<geode_analysis*>(handle); }
geode_drums* drumsOf(jlong handle) { return reinterpret_cast<geode_drums*>(handle); }

void writeFrame(JNIEnv* env, const GeodeFeatureFrame& frame, jfloatArray out) {
    if (!out || env->GetArrayLength(out) < GEODE_FEATURE_FRAME_FLOATS) return;
    env->SetFloatArrayRegion(out, 0, GEODE_FEATURE_FRAME_FLOATS, reinterpret_cast<const jfloat*>(&frame));
}

}  // namespace

extern "C" {

JNIEXPORT jstring JNICALL
Java_dev_geode_engine_bridge_GeodeNative_version(JNIEnv* env, jobject) {
    return env->NewStringUTF(geode_version());
}

JNIEXPORT jint JNICALL
Java_dev_geode_engine_bridge_GeodeNative_featureFrameFloats(JNIEnv*, jobject) {
    return GEODE_FEATURE_FRAME_FLOATS;
}

JNIEXPORT jlong JNICALL
Java_dev_geode_engine_bridge_GeodeNative_analysisCreate(JNIEnv*, jobject, jint sampleRate, jint fftSize, jfloat hopRateHz) {
    return reinterpret_cast<jlong>(geode_analysis_create(sampleRate, fftSize, hopRateHz));
}

JNIEXPORT void JNICALL
Java_dev_geode_engine_bridge_GeodeNative_analysisDestroy(JNIEnv*, jobject, jlong handle) {
    geode_analysis_destroy(analysisOf(handle));
}

JNIEXPORT void JNICALL
Java_dev_geode_engine_bridge_GeodeNative_analysisSetSampleRate(JNIEnv*, jobject, jlong handle, jint sampleRate) {
    geode_analysis_set_sample_rate(analysisOf(handle), sampleRate);
}

JNIEXPORT void JNICALL
Java_dev_geode_engine_bridge_GeodeNative_analysisSetTuning(JNIEnv*, jobject, jlong handle, jfloat sensitivity,
                                                           jfloat refractoryMs, jfloat attackSeconds, jfloat releaseSeconds) {
    geode_analysis_set_tuning(analysisOf(handle), sensitivity, refractoryMs, attackSeconds, releaseSeconds);
}

JNIEXPORT void JNICALL
Java_dev_geode_engine_bridge_GeodeNative_analysisReset(JNIEnv*, jobject, jlong handle) {
    geode_analysis_reset(analysisOf(handle));
}

JNIEXPORT void JNICALL
Java_dev_geode_engine_bridge_GeodeNative_analysisAnalyze(JNIEnv* env, jobject, jlong handle, jfloatArray mid,
                                                         jfloatArray side, jfloat dtSeconds, jfloatArray out) {
    FloatElements midData(env, mid);
    FloatElements sideData(env, side);
    if (!midData.get()) return;
    const jsize frames = sideData.get() && sideData.length() < midData.length() ? sideData.length() : midData.length();
    GeodeFeatureFrame frame{};
    geode_analysis_analyze(analysisOf(handle), midData.get(), sideData.get(), static_cast<size_t>(frames), dtSeconds, &frame);
    writeFrame(env, frame, out);
}

JNIEXPORT void JNICALL
Java_dev_geode_engine_bridge_GeodeNative_analysisPush(JNIEnv* env, jobject, jlong handle, jfloatArray interleaved,
                                                      jint frames, jint channels) {
    FloatElements data(env, interleaved);
    if (!data.get() || frames <= 0 || channels <= 0) return;
    const jint available = data.length() / channels;
    const jint count = frames < available ? frames : available;
    geode_analysis_push(analysisOf(handle), data.get(), static_cast<size_t>(count), channels);
}

JNIEXPORT jboolean JNICALL
Java_dev_geode_engine_bridge_GeodeNative_analysisPull(JNIEnv* env, jobject, jlong handle, jfloatArray out) {
    GeodeFeatureFrame frame{};
    if (!geode_analysis_pull(analysisOf(handle), &frame)) return JNI_FALSE;
    writeFrame(env, frame, out);
    return JNI_TRUE;
}

JNIEXPORT jstring JNICALL
Java_dev_geode_engine_bridge_GeodeNative_analysisKey(JNIEnv* env, jobject, jlong handle) {
    std::array<char, 32> key{};
    geode_analysis_key(analysisOf(handle), key.data(), key.size());
    return env->NewStringUTF(key.data());
}

JNIEXPORT void JNICALL
Java_dev_geode_engine_bridge_GeodeNative_pulseReplay(JNIEnv* env, jobject, jfloatArray flux, jfloatArray rms,
                                                     jfloat hopRateHz, jfloat sensitivity, jfloat refractoryMs, jfloatArray out) {
    FloatElements fluxData(env, flux);
    FloatElements rmsData(env, rms);
    if (!fluxData.get() || !out) return;
    const jsize count = fluxData.length();
    if (env->GetArrayLength(out) < count * 6) return;
    std::vector<float> frames(static_cast<size_t>(count) * 6);
    geode_pulse_replay(fluxData.get(), static_cast<size_t>(count), rmsData.get(), static_cast<size_t>(rmsData.length()),
                       hopRateHz, sensitivity, refractoryMs, frames.data());
    env->SetFloatArrayRegion(out, 0, count * 6, frames.data());
}

JNIEXPORT jlong JNICALL
Java_dev_geode_engine_bridge_GeodeNative_drumsCreate(JNIEnv*, jobject, jint bandCount, jfloat hopRateHz, jint sampleRate) {
    return reinterpret_cast<jlong>(geode_drums_create(bandCount, hopRateHz, sampleRate));
}

JNIEXPORT void JNICALL
Java_dev_geode_engine_bridge_GeodeNative_drumsDestroy(JNIEnv*, jobject, jlong handle) {
    geode_drums_destroy(drumsOf(handle));
}

JNIEXPORT void JNICALL
Java_dev_geode_engine_bridge_GeodeNative_drumsStep(JNIEnv* env, jobject, jlong handle, jfloatArray bands, jfloatArray out) {
    FloatElements data(env, bands);
    if (!data.get() || !out || env->GetArrayLength(out) < 3) return;
    std::array<float, 3> impulses{};
    geode_drums_step(drumsOf(handle), data.get(), impulses.data());
    env->SetFloatArrayRegion(out, 0, 3, impulses.data());
}

}
