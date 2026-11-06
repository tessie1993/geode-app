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

// The live wallpaper's and the dream's run rule (viz/SurfaceRun), beside their silence rule.

JNIEXPORT jint JNICALL
Java_dev_geode_engine_bridge_GeodeNative_surfaceRunMode(JNIEnv*, jobject, jint flags, jlong animateUntilMs, jlong nowMs) {
    return geode_surface_run_mode(static_cast<unsigned>(flags), animateUntilMs, nowMs);
}

JNIEXPORT jboolean JNICALL
Java_dev_geode_engine_bridge_GeodeNative_surfaceListening(JNIEnv*, jobject, jint flags, jlong animateUntilMs,
                                                          jlong nowMs) {
    return geode_surface_listening(static_cast<unsigned>(flags), animateUntilMs, nowMs) ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jboolean JNICALL
Java_dev_geode_engine_bridge_GeodeNative_surfaceEntitled(JNIEnv*, jobject, jlong animateUntilMs, jlong nowMs) {
    return geode_surface_entitled(animateUntilMs, nowMs) ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jlong JNICALL
Java_dev_geode_engine_bridge_GeodeNative_surfaceRecheckInMs(JNIEnv*, jobject, jlong animateUntilMs, jlong nowMs) {
    return geode_surface_recheck_in_ms(animateUntilMs, nowMs);
}

}  // extern "C"
