#include <jni.h>

#include "api/geode_api.h"

// The playout clock (core/audio/PlayoutClock, #349): values in, values out, nothing else.

namespace {
geode_playout* playoutOf(jlong handle) { return reinterpret_cast<geode_playout*>(handle); }
}  // namespace

extern "C" {

JNIEXPORT jlong JNICALL Java_dev_geode_engine_bridge_GeodeNative_playoutCreate(JNIEnv*, jobject) {
    return reinterpret_cast<jlong>(geode_playout_create());
}

JNIEXPORT void JNICALL Java_dev_geode_engine_bridge_GeodeNative_playoutReset(JNIEnv*, jobject, jlong handle) {
    if (handle) geode_playout_reset(playoutOf(handle));
}

JNIEXPORT void JNICALL Java_dev_geode_engine_bridge_GeodeNative_playoutBeginEpoch(JNIEnv*, jobject, jlong handle,
                                                                                  jint epoch, jint rateHz) {
    if (handle) geode_playout_begin_epoch(playoutOf(handle), epoch, rateHz);
}

JNIEXPORT void JNICALL Java_dev_geode_engine_bridge_GeodeNative_playoutBuffer(JNIEnv*, jobject, jlong handle,
                                                                              jlong mediaUs) {
    if (handle) geode_playout_buffer(playoutOf(handle), mediaUs);
}

JNIEXPORT void JNICALL Java_dev_geode_engine_bridge_GeodeNative_playoutFlush(JNIEnv*, jobject, jlong handle) {
    if (handle) geode_playout_flush(playoutOf(handle));
}

JNIEXPORT void JNICALL Java_dev_geode_engine_bridge_GeodeNative_playoutPosition(JNIEnv*, jobject, jlong handle,
                                                                                jlong mediaUs, jlong nowNs) {
    if (handle) geode_playout_position(playoutOf(handle), mediaUs, nowNs);
}

JNIEXPORT void JNICALL Java_dev_geode_engine_bridge_GeodeNative_playoutPlaying(JNIEnv*, jobject, jlong handle,
                                                                               jboolean playing, jlong nowNs) {
    if (handle) geode_playout_playing(playoutOf(handle), playing ? 1 : 0, nowNs);
}

JNIEXPORT void JNICALL Java_dev_geode_engine_bridge_GeodeNative_playoutSpeed(JNIEnv*, jobject, jlong handle,
                                                                             jfloat speed) {
    if (handle) geode_playout_speed(playoutOf(handle), speed);
}

JNIEXPORT jlong JNICALL Java_dev_geode_engine_bridge_GeodeNative_playoutHeardFrame(JNIEnv*, jobject, jlong handle,
                                                                                   jint epoch, jlong written,
                                                                                   jlong nowNs) {
    return handle ? geode_playout_heard_frame(playoutOf(handle), epoch, written, nowNs) : written;
}

}  // extern "C"
