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

JNIEXPORT jint JNICALL
Java_dev_geode_engine_bridge_GeodeNative_soundSourceStep(JNIEnv* env, jobject, jlongArray state, jlong nowMs,
                                                         jboolean ringPresent, jlong ringId, jlong ringWritten,
                                                         jboolean capturePermitted, jboolean captureRunning) {
    if (!state || env->GetArrayLength(state) < GEODE_SOUND_SOURCE_STATE_LONGS) return 0;
    long long s[GEODE_SOUND_SOURCE_STATE_LONGS];
    env->GetLongArrayRegion(state, 0, GEODE_SOUND_SOURCE_STATE_LONGS, reinterpret_cast<jlong*>(s));
    const int out = geode_sound_source_step(s, nowMs, ringPresent ? 1 : 0, ringId, ringWritten,
                                            capturePermitted ? 1 : 0, captureRunning ? 1 : 0);
    env->SetLongArrayRegion(state, 0, GEODE_SOUND_SOURCE_STATE_LONGS, reinterpret_cast<const jlong*>(s));
    return out;
}

JNIEXPORT void JNICALL
Java_dev_geode_engine_bridge_GeodeNative_soundSourceStartFailed(JNIEnv* env, jobject, jlongArray state, jlong nowMs) {
    if (!state || env->GetArrayLength(state) < GEODE_SOUND_SOURCE_STATE_LONGS) return;
    long long s[GEODE_SOUND_SOURCE_STATE_LONGS];
    env->GetLongArrayRegion(state, 0, GEODE_SOUND_SOURCE_STATE_LONGS, reinterpret_cast<jlong*>(s));
    geode_sound_source_start_failed(s, nowMs);
    env->SetLongArrayRegion(state, 0, GEODE_SOUND_SOURCE_STATE_LONGS, reinterpret_cast<const jlong*>(s));
}

}  // extern "C"
