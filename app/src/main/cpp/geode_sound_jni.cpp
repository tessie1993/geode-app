#include <jni.h>

#include <algorithm>

#include "api/geode_api.h"
#include "jni_util.hpp"

extern "C" {

JNIEXPORT jlong JNICALL
Java_dev_geode_engine_bridge_GeodeNative_silenceHeardAt(JNIEnv* env, jobject, jfloatArray left, jfloatArray right,
                                                        jint frames, jlong nowMs, jlong heardAtMs) {
    geode::jni::FloatElements l(env, left);
    geode::jni::FloatElements r(env, right);
    if (!l.get() || !r.get()) return heardAtMs;
    const int n = std::min({static_cast<int>(frames), static_cast<int>(l.length()), static_cast<int>(r.length())});
    const float* channels[2] = {l.get(), r.get()};
    return geode_silence_heard_at(channels, 2, n, nowMs, heardAtMs);
}

JNIEXPORT jboolean JNICALL
Java_dev_geode_engine_bridge_GeodeNative_silenceResting(JNIEnv*, jobject, jlong nowMs, jlong heardAtMs) {
    return geode_silence_resting(nowMs, heardAtMs) ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jlong JNICALL
Java_dev_geode_engine_bridge_GeodeNative_soundTrackBoundary(JNIEnv*, jobject, jlong seed) {
    return geode_sound_track_boundary(seed);
}

JNIEXPORT void JNICALL
Java_dev_geode_engine_bridge_GeodeNative_soundSetFadeGain(JNIEnv*, jobject, jfloat gain) {
    geode_sound_set_fade_gain(gain);
}

JNIEXPORT void JNICALL
Java_dev_geode_engine_bridge_GeodeNative_soundSetPlayerSource(JNIEnv*, jobject, jboolean on) {
    geode_sound_set_player_source(on == JNI_TRUE ? 1 : 0);
}

}  // extern "C"
