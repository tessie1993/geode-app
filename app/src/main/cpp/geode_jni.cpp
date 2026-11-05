#include <jni.h>

#include "api/geode_api.h"

namespace {

geode_dsp* dspOf(jlong handle) { return reinterpret_cast<geode_dsp*>(handle); }
geode_replaygain* replayGainOf(jlong table) { return reinterpret_cast<geode_replaygain*>(table); }

}  // namespace

extern "C" {

JNIEXPORT jlong JNICALL
Java_dev_geode_engine_bridge_GeodeNative_dspCreate(JNIEnv*, jobject, jint sampleRate, jint channels) {
    return reinterpret_cast<jlong>(geode_dsp_create(sampleRate, channels));
}

JNIEXPORT void JNICALL
Java_dev_geode_engine_bridge_GeodeNative_dspDestroy(JNIEnv*, jobject, jlong handle) {
    geode_dsp_destroy(dspOf(handle));
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

JNIEXPORT void JNICALL
Java_dev_geode_engine_bridge_GeodeNative_dspSetBassBoost(JNIEnv*, jobject, jlong handle, jint permille) {
    geode_dsp_set_bass_boost(dspOf(handle), permille);
}

JNIEXPORT void JNICALL
Java_dev_geode_engine_bridge_GeodeNative_dspSetLoudnessMb(JNIEnv*, jobject, jlong handle, jint millibels) {
    geode_dsp_set_loudness_mb(dspOf(handle), millibels);
}

JNIEXPORT jlong JNICALL
Java_dev_geode_engine_bridge_GeodeNative_replayGainCreate(JNIEnv*, jobject) {
    return reinterpret_cast<jlong>(geode_replaygain_create());
}

JNIEXPORT void JNICALL
Java_dev_geode_engine_bridge_GeodeNative_replayGainDestroy(JNIEnv*, jobject, jlong table) {
    geode_replaygain_destroy(replayGainOf(table));
}

JNIEXPORT void JNICALL
Java_dev_geode_engine_bridge_GeodeNative_replayGainConfigure(JNIEnv*, jobject, jlong table, jint mode, jfloat preampDb,
                                                             jboolean clipGuard) {
    geode_replaygain_configure(replayGainOf(table), mode, preampDb, clipGuard == JNI_TRUE);
}

JNIEXPORT void JNICALL
Java_dev_geode_engine_bridge_GeodeNative_replayGainSetTags(JNIEnv* env, jobject, jlong table, jstring stream, jint mask,
                                                           jfloatArray values) {
    if (!stream || !values || env->GetArrayLength(values) < 4) return;
    jfloat v[4];
    env->GetFloatArrayRegion(values, 0, 4, v);
    const char* name = env->GetStringUTFChars(stream, nullptr);
    if (!name) return;
    geode_replaygain_set_tags(replayGainOf(table), name, mask, v);
    env->ReleaseStringUTFChars(stream, name);
}

JNIEXPORT void JNICALL
Java_dev_geode_engine_bridge_GeodeNative_dspSetReplayGain(JNIEnv*, jobject, jlong handle, jlong table) {
    geode_dsp_set_replaygain(dspOf(handle), replayGainOf(table));
}

JNIEXPORT void JNICALL
Java_dev_geode_engine_bridge_GeodeNative_dspBeginStream(JNIEnv* env, jobject, jlong handle, jstring stream) {
    if (!stream) {
        geode_dsp_begin_stream(dspOf(handle), nullptr);
        return;
    }
    const char* name = env->GetStringUTFChars(stream, nullptr);
    geode_dsp_begin_stream(dspOf(handle), name);
    if (name) env->ReleaseStringUTFChars(stream, name);
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

}
