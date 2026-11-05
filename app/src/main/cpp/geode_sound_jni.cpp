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

// Live input's state machine (audio/LiveInput): the next state, and the actions packed above bit 8.

JNIEXPORT jlong JNICALL
Java_dev_geode_engine_bridge_GeodeNative_liveInputStep(JNIEnv*, jobject, jint state, jint event, jboolean hasPermission) {
    unsigned actions = 0;
    const int next = geode_live_input_step(state, event, hasPermission ? 1 : 0, &actions);
    return (static_cast<jlong>(actions) << 8) | static_cast<jlong>(next & 0xff);
}

JNIEXPORT jboolean JNICALL
Java_dev_geode_engine_bridge_GeodeNative_liveInputIsOn(JNIEnv*, jobject, jint state) {
    return geode_live_input_is_on(state) ? JNI_TRUE : JNI_FALSE;
}

}  // extern "C"
