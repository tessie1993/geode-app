// JNI for Auto visuals' decisions (core/viz/AutoPlan through geode_auto_*). Marshals only.
#include <jni.h>

#include <vector>

#include "api/geode_api.h"
#include "jni_util.hpp"

namespace {

using geode::jni::Utf8Chars;
using geode::jni::utf8String;

std::vector<float> floatsOf(JNIEnv* env, jfloatArray array) {
    std::vector<float> out;
    if (!array) return out;
    out.resize(static_cast<size_t>(env->GetArrayLength(array)));
    if (!out.empty()) env->GetFloatArrayRegion(array, 0, static_cast<jsize>(out.size()), out.data());
    return out;
}

}  // namespace

extern "C" {

JNIEXPORT jint JNICALL
Java_dev_geode_engine_bridge_AutoNative_autoIntervalSeconds(JNIEnv*, jobject, jint requested) {
    return geode_auto_interval_s(requested);
}

JNIEXPORT void JNICALL
Java_dev_geode_engine_bridge_AutoNative_autoEditingSet(JNIEnv*, jobject, jboolean on, jlong nowMs) {
    geode_auto_editing_set(on ? 1 : 0, nowMs);
}

JNIEXPORT void JNICALL
Java_dev_geode_engine_bridge_AutoNative_autoHandPick(JNIEnv*, jobject, jlong nowMs) {
    geode_auto_hand_pick(nowMs);
}

JNIEXPORT jint JNICALL
Java_dev_geode_engine_bridge_AutoNative_autoPlaylistTick(JNIEnv*, jobject, jint count, jboolean enabled, jboolean playing,
                                                          jint intervalSec, jlong nowMs) {
    return geode_auto_playlist_tick(count, enabled ? 1 : 0, playing ? 1 : 0, intervalSec, nowMs);
}

JNIEXPORT jint JNICALL
Java_dev_geode_engine_bridge_AutoNative_autoPlaylistStep(JNIEnv*, jobject, jint count, jint step, jlong nowMs) {
    return geode_auto_playlist_step(count, step, nowMs);
}

JNIEXPORT jboolean JNICALL
Java_dev_geode_engine_bridge_AutoNative_autoRandomTick(JNIEnv*, jobject, jboolean enabled, jboolean playing, jint intervalSec,
                                                        jlong nowMs) {
    return geode_auto_random_tick(enabled ? 1 : 0, playing ? 1 : 0, intervalSec, nowMs) ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jint JNICALL
Java_dev_geode_engine_bridge_AutoNative_autoMoment(JNIEnv* env, jobject, jstring style, jfloatArray params, jstring paletteId,
                                                    jboolean playing, jboolean playlistEnabled) {
    Utf8Chars id(env, style);
    Utf8Chars palette(env, paletteId);
    const std::vector<float> frame = floatsOf(env, params);
    return geode_auto_moment(id.get(), frame.data(), static_cast<int>(frame.size()), palette.get(), playing ? 1 : 0,
                             playlistEnabled ? 1 : 0);
}

JNIEXPORT void JNICALL
Java_dev_geode_engine_bridge_AutoNative_autoLeftAs(JNIEnv* env, jobject, jstring style, jfloatArray params, jstring paletteId) {
    Utf8Chars id(env, style);
    Utf8Chars palette(env, paletteId);
    const std::vector<float> frame = floatsOf(env, params);
    geode_auto_left_as(id.get(), frame.data(), static_cast<int>(frame.size()), palette.get());
}

// out[0] the entry's kind, out[1] its index in that list; null for none.
JNIEXPORT jstring JNICALL
Java_dev_geode_engine_bridge_AutoNative_autoDraw(JNIEnv* env, jobject, jstring lockedStyles, jstring current, jstring presetStyles,
                                                  jint presetOnScreen,
                                                  jint sources, jlong nowMs, jintArray out) {
    if (!out || env->GetArrayLength(out) < 2) return nullptr;
    Utf8Chars locked(env, lockedStyles);
    Utf8Chars style(env, current);
    Utf8Chars looks(env, presetStyles);
    int picked[2] = {-1, -1};
    const char* id = geode_auto_draw(locked.get(), style.get(), looks.get(), presetOnScreen,
                                     sources, nowMs, picked);
    const jint values[2] = {picked[0], picked[1]};
    env->SetIntArrayRegion(out, 0, 2, values);
    return utf8String(env, id);
}

}  // extern "C"
