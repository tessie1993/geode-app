#include <jni.h>

#include "api/geode_api.h"

namespace {

geode_dsp* dspOf(jlong handle) { return reinterpret_cast<geode_dsp*>(handle); }

}  // namespace

extern "C" {

JNIEXPORT jstring JNICALL
Java_dev_geode_engine_bridge_GeodeNative_version(JNIEnv* env, jobject) {
    return env->NewStringUTF(geode_version());
}

JNIEXPORT jlong JNICALL
Java_dev_geode_engine_bridge_GeodeNative_dspCreate(JNIEnv*, jobject, jint sampleRate, jint channels) {
    return reinterpret_cast<jlong>(geode_dsp_create(sampleRate, channels));
}

JNIEXPORT void JNICALL
Java_dev_geode_engine_bridge_GeodeNative_dspDestroy(JNIEnv*, jobject, jlong handle) {
    geode_dsp_destroy(dspOf(handle));
}

JNIEXPORT jint JNICALL
Java_dev_geode_engine_bridge_GeodeNative_dspBandCount(JNIEnv*, jobject) {
    return geode_dsp_band_count();
}

JNIEXPORT jfloat JNICALL
Java_dev_geode_engine_bridge_GeodeNative_dspBandCenterHz(JNIEnv*, jobject, jint band) {
    return geode_dsp_band_center_hz(band);
}

JNIEXPORT void JNICALL
Java_dev_geode_engine_bridge_GeodeNative_dspSetEnabled(JNIEnv*, jobject, jlong handle, jboolean enabled) {
    geode_dsp_set_enabled(dspOf(handle), enabled == JNI_TRUE);
}

JNIEXPORT void JNICALL
Java_dev_geode_engine_bridge_GeodeNative_dspSetBand(JNIEnv*, jobject, jlong handle, jint band, jint millibels) {
    geode_dsp_set_band(dspOf(handle), band, millibels);
}

JNIEXPORT jint JNICALL
Java_dev_geode_engine_bridge_GeodeNative_dspBand(JNIEnv*, jobject, jlong handle, jint band) {
    return geode_dsp_band(dspOf(handle), band);
}

JNIEXPORT void JNICALL
Java_dev_geode_engine_bridge_GeodeNative_dspSetBassBoost(JNIEnv*, jobject, jlong handle, jint permille) {
    geode_dsp_set_bass_boost(dspOf(handle), permille);
}

JNIEXPORT void JNICALL
Java_dev_geode_engine_bridge_GeodeNative_dspSetLoudnessMb(JNIEnv*, jobject, jlong handle, jint millibels) {
    geode_dsp_set_loudness_mb(dspOf(handle), millibels);
}

JNIEXPORT void JNICALL
Java_dev_geode_engine_bridge_GeodeNative_dspSetGainDb(JNIEnv*, jobject, jlong handle, jfloat db) {
    geode_dsp_set_gain_db(dspOf(handle), db);
}

JNIEXPORT void JNICALL
Java_dev_geode_engine_bridge_GeodeNative_dspSetCrossfeed(JNIEnv*, jobject, jlong handle, jboolean enabled) {
    geode_dsp_set_crossfeed(dspOf(handle), enabled == JNI_TRUE);
}

JNIEXPORT void JNICALL
Java_dev_geode_engine_bridge_GeodeNative_dspSetLimiter(JNIEnv*, jobject, jlong handle, jboolean enabled) {
    geode_dsp_set_limiter(dspOf(handle), enabled == JNI_TRUE);
}

JNIEXPORT void JNICALL
Java_dev_geode_engine_bridge_GeodeNative_dspReset(JNIEnv*, jobject, jlong handle) {
    geode_dsp_reset(dspOf(handle));
}

// The buffer must be direct: the audio thread must not copy or allocate here.
JNIEXPORT void JNICALL
Java_dev_geode_engine_bridge_GeodeNative_dspProcess(JNIEnv* env, jobject, jlong handle, jobject buffer, jint frames) {
    if (!buffer) return;
    auto* samples = static_cast<float*>(env->GetDirectBufferAddress(buffer));
    if (!samples || frames <= 0) return;
    geode_dsp_process(dspOf(handle), samples, static_cast<size_t>(frames));
}

JNIEXPORT void JNICALL
Java_dev_geode_engine_bridge_GeodeNative_dspSetSampleRate(JNIEnv*, jobject, jlong handle, jint sampleRate) {
    geode_dsp_set_sample_rate(dspOf(handle), sampleRate);
}

JNIEXPORT jint JNICALL
Java_dev_geode_engine_bridge_GeodeNative_dspSampleRate(JNIEnv*, jobject, jlong handle) {
    return geode_dsp_sample_rate(dspOf(handle));
}

}
