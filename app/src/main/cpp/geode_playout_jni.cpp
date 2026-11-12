#include <jni.h>

#include "api/geode_api.h"

// The playout clock (core/sound/interp/PlayoutClock, #349): values in, values out, nothing else.

namespace {
geode_playout* playoutOf(jlong handle) { return reinterpret_cast<geode_playout*>(handle); }
}  // namespace

extern "C" {

JNIEXPORT jlong JNICALL Java_dev_geode_engine_bridge_GeodeNative_playoutCreate(JNIEnv*, jobject) {
    return reinterpret_cast<jlong>(geode_playout_create());
}

JNIEXPORT void JNICALL Java_dev_geode_engine_bridge_GeodeNative_playoutDestroy(JNIEnv*, jobject, jlong handle) {
    geode_playout_destroy(playoutOf(handle));
}

JNIEXPORT void JNICALL Java_dev_geode_engine_bridge_GeodeNative_playoutBufferHandled(JNIEnv*, jobject, jlong handle,
                                                                                     jlong presentationUs,
                                                                                     jlong ringFrame, jlong frames) {
    geode_playout_buffer_handled(playoutOf(handle), presentationUs, ringFrame, frames);
}

JNIEXPORT void JNICALL Java_dev_geode_engine_bridge_GeodeNative_playoutPosition(JNIEnv*, jobject, jlong handle,
                                                                                jlong sinkUs, jlong nowNs) {
    geode_playout_position(playoutOf(handle), sinkUs, nowNs);
}

JNIEXPORT void JNICALL Java_dev_geode_engine_bridge_GeodeNative_playoutPlaying(JNIEnv*, jobject, jlong handle,
                                                                               jboolean playing, jlong nowNs) {
    geode_playout_playing(playoutOf(handle), playing ? 1 : 0, nowNs);
}

JNIEXPORT void JNICALL Java_dev_geode_engine_bridge_GeodeNative_playoutFlush(JNIEnv*, jobject, jlong handle) {
    geode_playout_flush(playoutOf(handle));
}

JNIEXPORT void JNICALL Java_dev_geode_engine_bridge_GeodeNative_playoutSpeed(JNIEnv*, jobject, jlong handle,
                                                                             jfloat speed) {
    geode_playout_speed(playoutOf(handle), speed);
}

JNIEXPORT void JNICALL Java_dev_geode_engine_bridge_GeodeNative_playoutSetOffsetMs(JNIEnv*, jobject, jlong handle,
                                                                                   jint ms) {
    geode_playout_set_offset_ms(playoutOf(handle), ms);
}

JNIEXPORT jint JNICALL Java_dev_geode_engine_bridge_GeodeNative_playoutMaxOffsetMs(JNIEnv*, jobject) {
    return geode_playout_max_offset_ms();
}

JNIEXPORT jlong JNICALL Java_dev_geode_engine_bridge_GeodeNative_playoutSpeakerFrame(JNIEnv*, jobject, jlong handle,
                                                                                    jlong nowNs) {
    return geode_playout_speaker_frame(playoutOf(handle), nowNs);
}

JNIEXPORT void JNICALL Java_dev_geode_engine_bridge_GeodeNative_playoutSetBypass(JNIEnv*, jobject, jlong handle,
                                                                                 jboolean on) {
    geode_playout_set_bypass(playoutOf(handle), on ? 1 : 0);
}

JNIEXPORT jint JNICALL Java_dev_geode_engine_bridge_GeodeNative_playoutLagMs(JNIEnv*, jobject, jlong handle,
                                                                             jlong nowNs) {
    return geode_playout_lag_ms(playoutOf(handle), nowNs);
}

JNIEXPORT jint JNICALL Java_dev_geode_engine_bridge_GeodeNative_playoutRingFrames(JNIEnv*, jobject) {
    return geode_playout_ring_frames();
}

JNIEXPORT jint JNICALL Java_dev_geode_engine_bridge_GeodeNative_playoutRingRunway(JNIEnv*, jobject) {
    return geode_playout_ring_runway();
}

}  // extern "C"
