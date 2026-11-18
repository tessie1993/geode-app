#include <jni.h>

#include "api/geode_api.h"

// The playout clock (core/sound/interp/PlayoutClock, #349): values in, values out, nothing else.

namespace {
geode_playout* playoutOf(jlong handle) { return reinterpret_cast<geode_playout*>(handle); }
}  // namespace

extern "C" {

JNIEXPORT jlong JNICALL Java_dev_geode_engine_bridge_PlayoutNative_playoutCreate(JNIEnv*, jobject) {
    return reinterpret_cast<jlong>(geode_playout_create());
}

JNIEXPORT void JNICALL Java_dev_geode_engine_bridge_PlayoutNative_playoutDestroy(JNIEnv*, jobject, jlong handle) {
    geode_playout_destroy(playoutOf(handle));
}

JNIEXPORT void JNICALL Java_dev_geode_engine_bridge_PlayoutNative_playoutRingSpan(JNIEnv*, jobject, jlong handle,
                                                                                jlong presentationUs,
                                                                                jlong headBefore, jlong headAfter) {
    geode_playout_ring_span(playoutOf(handle), presentationUs, headBefore, headAfter);
}

JNIEXPORT void JNICALL Java_dev_geode_engine_bridge_PlayoutNative_playoutPosition(JNIEnv*, jobject, jlong handle,
                                                                                jlong sinkUs, jlong nowNs) {
    geode_playout_position(playoutOf(handle), sinkUs, nowNs);
}

JNIEXPORT void JNICALL Java_dev_geode_engine_bridge_PlayoutNative_playoutPlaying(JNIEnv*, jobject, jlong handle,
                                                                               jboolean playing, jlong nowNs) {
    geode_playout_playing(playoutOf(handle), playing ? 1 : 0, nowNs);
}

JNIEXPORT void JNICALL Java_dev_geode_engine_bridge_PlayoutNative_playoutFlush(JNIEnv*, jobject, jlong handle) {
    geode_playout_flush(playoutOf(handle));
}

JNIEXPORT void JNICALL Java_dev_geode_engine_bridge_PlayoutNative_playoutSpeed(JNIEnv*, jobject, jlong handle,
                                                                             jfloat speed) {
    geode_playout_speed(playoutOf(handle), speed);
}

JNIEXPORT void JNICALL Java_dev_geode_engine_bridge_PlayoutNative_playoutSetOffsetMs(JNIEnv*, jobject, jlong handle,
                                                                                   jint ms) {
    geode_playout_set_offset_ms(playoutOf(handle), ms);
}

JNIEXPORT jint JNICALL Java_dev_geode_engine_bridge_PlayoutNative_playoutMaxOffsetMs(JNIEnv*, jobject) {
    return geode_playout_max_offset_ms();
}

JNIEXPORT void JNICALL Java_dev_geode_engine_bridge_PlayoutNative_playoutSetBypass(JNIEnv*, jobject, jlong handle,
                                                                                 jboolean on) {
    geode_playout_set_bypass(playoutOf(handle), on ? 1 : 0);
}

JNIEXPORT jint JNICALL Java_dev_geode_engine_bridge_PlayoutNative_playoutLagMs(JNIEnv*, jobject, jlong handle,
                                                                             jlong nowNs) {
    return geode_playout_lag_ms(playoutOf(handle), nowNs);
}

}  // extern "C"
