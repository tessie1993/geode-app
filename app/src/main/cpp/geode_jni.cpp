#include <jni.h>

#include "api/geode_api.h"

namespace {

geode_dsp* dspOf(jlong handle) { return reinterpret_cast<geode_dsp*>(handle); }

}  // namespace

extern "C" {

JNIEXPORT jlong JNICALL
Java_dev_geode_engine_bridge_GeodeNative_dspCreate(JNIEnv*, jobject) {
    return reinterpret_cast<jlong>(geode_dsp_create());
}

JNIEXPORT jboolean JNICALL
Java_dev_geode_engine_bridge_GeodeNative_dspConfigure(JNIEnv*, jobject, jlong handle, jint sampleRate, jint channels) {
    return geode_dsp_configure(dspOf(handle), sampleRate, channels) != 0 ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT void JNICALL
Java_dev_geode_engine_bridge_GeodeNative_dspFlush(JNIEnv*, jobject, jlong handle) {
    geode_dsp_flush(dspOf(handle));
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
Java_dev_geode_engine_bridge_GeodeNative_dspSetBitPerfect(JNIEnv*, jobject, jlong handle, jboolean on) {
    geode_dsp_set_bit_perfect(dspOf(handle), on == JNI_TRUE);
}

// The buffer must be direct: the audio thread must not copy or allocate here. Its capacity goes along so
// native can never walk past it (issue #356). `pcmFloat` only picks which of the two ABI calls gets it.
JNIEXPORT void JNICALL
Java_dev_geode_engine_bridge_GeodeNative_dspProcess(JNIEnv* env, jobject, jlong handle, jobject buffer, jint frames, jint channels,
                                                    jboolean pcmFloat) {
    if (!buffer) return;
    void* address = env->GetDirectBufferAddress(buffer);
    const jlong capacityBytes = env->GetDirectBufferCapacity(buffer);
    if (!address || capacityBytes <= 0 || frames <= 0 || channels <= 0) return;
    const auto bytes = static_cast<size_t>(capacityBytes);
    if (pcmFloat == JNI_TRUE) {
        geode_dsp_process(dspOf(handle), static_cast<float*>(address), bytes / sizeof(float), static_cast<size_t>(frames), channels);
    } else {
        geode_dsp_process_pcm16(dspOf(handle), static_cast<int16_t*>(address), bytes / sizeof(int16_t), static_cast<size_t>(frames), channels);
    }
}

}
