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

// Playback maths (core/audio/PlaybackMath): values in, values out.

JNIEXPORT jfloat JNICALL
Java_dev_geode_engine_bridge_GeodeNative_playbackSemitonesToRatio(JNIEnv*, jobject, jfloat semitones) {
    return geode_playback_semitones_to_ratio(semitones);
}

JNIEXPORT jlong JNICALL
Java_dev_geode_engine_bridge_GeodeNative_playbackSleepFadeMs(JNIEnv*, jobject) {
    return geode_playback_sleep_fade_ms();
}

JNIEXPORT jfloat JNICALL
Java_dev_geode_engine_bridge_GeodeNative_playbackSleepFadeVolume(JNIEnv*, jobject, jlong remainingMs, jlong fadeMs) {
    return geode_playback_sleep_fade_volume(remainingMs, fadeMs);
}

JNIEXPORT jfloat JNICALL
Java_dev_geode_engine_bridge_GeodeNative_playbackSnap(JNIEnv*, jobject, jfloat value, jfloat step) {
    return geode_playback_snap(value, step);
}

}  // extern "C"
