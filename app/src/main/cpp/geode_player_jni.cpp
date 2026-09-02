#include <jni.h>

#include <array>

#include "api/geode_api.h"

namespace {

geode_player* playerOf(jlong handle) { return reinterpret_cast<geode_player*>(handle); }

}  // namespace

extern "C" {

JNIEXPORT jlong JNICALL
Java_dev_geode_engine_bridge_GeodeNative_playerCreate(JNIEnv*, jobject) {
    return reinterpret_cast<jlong>(geode_player_create());
}

JNIEXPORT void JNICALL
Java_dev_geode_engine_bridge_GeodeNative_playerDestroy(JNIEnv*, jobject, jlong handle) {
    geode_player_destroy(playerOf(handle));
}

JNIEXPORT void JNICALL
Java_dev_geode_engine_bridge_GeodeNative_playerOpen(JNIEnv*, jobject, jlong handle, jint fd, jlong offset, jlong length,
                                                    jlong token) {
    geode_player_open(playerOf(handle), fd, offset, length, token);
}

JNIEXPORT void JNICALL
Java_dev_geode_engine_bridge_GeodeNative_playerSetNext(JNIEnv*, jobject, jlong handle, jint fd, jlong offset,
                                                       jlong length, jlong token) {
    geode_player_set_next(playerOf(handle), fd, offset, length, token);
}

JNIEXPORT void JNICALL
Java_dev_geode_engine_bridge_GeodeNative_playerPlay(JNIEnv*, jobject, jlong handle) {
    geode_player_play(playerOf(handle));
}

JNIEXPORT void JNICALL
Java_dev_geode_engine_bridge_GeodeNative_playerPause(JNIEnv*, jobject, jlong handle) {
    geode_player_pause(playerOf(handle));
}

JNIEXPORT void JNICALL
Java_dev_geode_engine_bridge_GeodeNative_playerStop(JNIEnv*, jobject, jlong handle) {
    geode_player_stop(playerOf(handle));
}

JNIEXPORT void JNICALL
Java_dev_geode_engine_bridge_GeodeNative_playerSeek(JNIEnv*, jobject, jlong handle, jlong positionUs) {
    geode_player_seek(playerOf(handle), positionUs);
}

JNIEXPORT void JNICALL
Java_dev_geode_engine_bridge_GeodeNative_playerSetCrossfade(JNIEnv*, jobject, jlong handle, jint durationMs, jint curve) {
    geode_player_set_crossfade(playerOf(handle), durationMs, curve);
}

JNIEXPORT void JNICALL
Java_dev_geode_engine_bridge_GeodeNative_playerSetDsp(JNIEnv*, jobject, jlong handle, jlong dsp) {
    geode_player_set_dsp(playerOf(handle), reinterpret_cast<geode_dsp*>(dsp));
}

JNIEXPORT void JNICALL
Java_dev_geode_engine_bridge_GeodeNative_playerSetVolume(JNIEnv*, jobject, jlong handle, jfloat volume) {
    geode_player_set_volume(playerOf(handle), volume);
}

JNIEXPORT jint JNICALL
Java_dev_geode_engine_bridge_GeodeNative_playerState(JNIEnv*, jobject, jlong handle) {
    return geode_player_state(playerOf(handle));
}

JNIEXPORT jboolean JNICALL
Java_dev_geode_engine_bridge_GeodeNative_playerPlayWhenReady(JNIEnv*, jobject, jlong handle) {
    return geode_player_play_when_ready(playerOf(handle)) ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jlong JNICALL
Java_dev_geode_engine_bridge_GeodeNative_playerPositionUs(JNIEnv*, jobject, jlong handle) {
    return geode_player_position_us(playerOf(handle));
}

JNIEXPORT jlong JNICALL
Java_dev_geode_engine_bridge_GeodeNative_playerDurationUs(JNIEnv*, jobject, jlong handle) {
    return geode_player_duration_us(playerOf(handle));
}

JNIEXPORT jlong JNICALL
Java_dev_geode_engine_bridge_GeodeNative_playerCurrentToken(JNIEnv*, jobject, jlong handle) {
    return geode_player_current_token(playerOf(handle));
}

JNIEXPORT jint JNICALL
Java_dev_geode_engine_bridge_GeodeNative_playerOutputSampleRate(JNIEnv*, jobject, jlong handle) {
    return geode_player_output_sample_rate(playerOf(handle));
}

JNIEXPORT jstring JNICALL
Java_dev_geode_engine_bridge_GeodeNative_playerLastError(JNIEnv* env, jobject, jlong handle) {
    std::array<char, 256> message{};
    geode_player_last_error(playerOf(handle), message.data(), message.size());
    return env->NewStringUTF(message.data());
}

// The buffer must be direct: the analysis thread reads straight out of the mixer's ring.
JNIEXPORT jint JNICALL
Java_dev_geode_engine_bridge_GeodeNative_playerReadTap(JNIEnv* env, jobject, jlong handle, jobject buffer, jint frames) {
    if (!buffer || frames <= 0) return 0;
    auto* out = static_cast<float*>(env->GetDirectBufferAddress(buffer));
    if (!out) return 0;
    const jlong capacityFrames = env->GetDirectBufferCapacity(buffer) / static_cast<jlong>(2 * sizeof(float));
    const size_t wanted = static_cast<size_t>(frames < capacityFrames ? frames : capacityFrames);
    return static_cast<jint>(geode_player_read_tap(playerOf(handle), out, wanted));
}

}  // extern "C"
