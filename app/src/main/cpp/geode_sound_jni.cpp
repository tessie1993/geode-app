#include <jni.h>

#include <algorithm>
#include <vector>

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
}

// The tuning schema in one array: every slot's default, then every minimum, then every maximum.
JNIEXPORT jfloatArray JNICALL
Java_dev_geode_engine_bridge_GeodeNative_soundTuningSchema(JNIEnv* env, jobject) {
    const int n = geode_sound_tuning_slots();
    std::vector<float> schema(static_cast<size_t>(3 * n));
    geode_sound_tuning_defaults(schema.data(), n);
    geode_sound_tuning_ranges(schema.data() + n, schema.data() + 2 * n, n);
    jfloatArray out = env->NewFloatArray(3 * n);
    if (!out) return nullptr;
    env->SetFloatArrayRegion(out, 0, 3 * n, schema.data());
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

JNIEXPORT void JNICALL
Java_dev_geode_engine_bridge_GeodeNative_soundTuningClamp(JNIEnv* env, jobject, jfloatArray values) {
    if (!values) return;
    const int n = std::min(static_cast<int>(env->GetArrayLength(values)), geode_sound_tuning_slots());
    std::vector<float> v(static_cast<size_t>(n));
    env->GetFloatArrayRegion(values, 0, n, v.data());
    geode_sound_tuning_clamp(v.data(), n);
    env->SetFloatArrayRegion(values, 0, n, v.data());
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

JNIEXPORT jlong JNICALL
Java_dev_geode_engine_bridge_GeodeNative_surfaceSettleMs(JNIEnv*, jobject, jint previousMode, jint mode) {
    return geode_surface_settle_ms(previousMode, mode);
}

JNIEXPORT jint JNICALL
Java_dev_geode_engine_bridge_GeodeNative_surfaceFpsChoice(JNIEnv*, jobject, jint requestedFps) {
    return geode_surface_fps_choice(requestedFps);
}

}  // extern "C"
