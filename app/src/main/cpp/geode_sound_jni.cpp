#include <jni.h>

#include <algorithm>
#include <string>
#include <vector>

#include "api/geode_api.h"
#include "jni_util.hpp"

extern "C" {

JNIEXPORT jboolean JNICALL
Java_dev_geode_engine_bridge_GeodeNative_silenceResting(JNIEnv*, jobject, jlong nowMs, jlong heardAtMs) {
    return geode_silence_resting(nowMs, heardAtMs) ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jlong JNICALL
Java_dev_geode_engine_bridge_GeodeNative_captureHeardAt(JNIEnv*, jobject, jfloat peak, jlong nowMs, jlong heardAtMs) {
    return geode_capture_heard_at(peak, nowMs, heardAtMs);
}

JNIEXPORT jboolean JNICALL
Java_dev_geode_engine_bridge_GeodeNative_captureQuiet(JNIEnv*, jobject, jlong nowMs, jlong heardAtMs) {
    return geode_capture_quiet(nowMs, heardAtMs) ? JNI_TRUE : JNI_FALSE;
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

JNIEXPORT jint JNICALL
Java_dev_geode_engine_bridge_GeodeNative_surfaceReason(JNIEnv*, jobject, jint flags, jlong animateUntilMs, jlong nowMs) {
    return geode_surface_reason(static_cast<unsigned>(flags), animateUntilMs, nowMs);
}

JNIEXPORT jint JNICALL
Java_dev_geode_engine_bridge_GeodeNative_stageRun(JNIEnv*, jobject, jint flags) {
    return static_cast<jint>(geode_stage_run(static_cast<unsigned>(flags)));
}

JNIEXPORT jint JNICALL
Java_dev_geode_engine_bridge_GeodeNative_stageHost(JNIEnv* env, jobject, jint flags, jintArray bounds, jint windowW,
                                                   jint windowH, jintArray rectOut) {
    if (bounds == nullptr || rectOut == nullptr || env->GetArrayLength(bounds) < 4 || env->GetArrayLength(rectOut) < 4) {
        return GEODE_STAGE_HOST_HIDDEN;
    }
    jint in[4];
    env->GetIntArrayRegion(bounds, 0, 4, in);
    int b[4] = {in[0], in[1], in[2], in[3]};
    int r[4] = {0, 0, 0, 0};
    const int alpha = geode_stage_host(static_cast<unsigned>(flags), b, windowW, windowH, r);
    const jint out[4] = {r[0], r[1], r[2], r[3]};
    env->SetIntArrayRegion(rectOut, 0, 4, out);
    return alpha;
}

JNIEXPORT jboolean JNICALL
Java_dev_geode_engine_bridge_GeodeNative_stageDisplayEligible(JNIEnv*, jobject, jint flags) {
    return geode_stage_display_eligible(static_cast<unsigned>(flags)) ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jint JNICALL
Java_dev_geode_engine_bridge_GeodeNative_stagePickDisplay(JNIEnv* env, jobject, jintArray ids, jintArray flags,
                                                          jint preferredId) {
    if (ids == nullptr || flags == nullptr) return -1;
    const jsize count = std::min(env->GetArrayLength(ids), env->GetArrayLength(flags));
    std::vector<int> idv(static_cast<size_t>(count));
    std::vector<unsigned> flagv(static_cast<size_t>(count));
    std::vector<jint> raw(static_cast<size_t>(count));
    if (count > 0) {
        env->GetIntArrayRegion(ids, 0, count, raw.data());
        for (jsize i = 0; i < count; ++i) idv[static_cast<size_t>(i)] = raw[static_cast<size_t>(i)];
        env->GetIntArrayRegion(flags, 0, count, raw.data());
        for (jsize i = 0; i < count; ++i) flagv[static_cast<size_t>(i)] = static_cast<unsigned>(raw[static_cast<size_t>(i)]);
    }
    return geode_stage_pick_display(idv.data(), flagv.data(), count, preferredId);
}

JNIEXPORT jboolean JNICALL
Java_dev_geode_engine_bridge_GeodeNative_stagePresents(JNIEnv*, jobject, jint flags) {
    return geode_stage_presents(static_cast<unsigned>(flags)) ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jint JNICALL
Java_dev_geode_engine_bridge_GeodeNative_stageStepTarget(JNIEnv*, jobject, jint page, jint flags, jint milkCount,
                                                         jint lookCount) {
    return geode_stage_step_target(page, static_cast<unsigned>(flags), milkCount, lookCount);
}

JNIEXPORT jint JNICALL
Java_dev_geode_engine_bridge_GeodeNative_stageStepIndex(JNIEnv* env, jobject, jbooleanArray open, jint here,
                                                        jint step) {
    if (open == nullptr) return -1;
    const jsize count = env->GetArrayLength(open);
    std::vector<jboolean> raw(static_cast<size_t>(count));
    if (count > 0) env->GetBooleanArrayRegion(open, 0, count, raw.data());
    std::vector<unsigned char> flags(raw.begin(), raw.end());
    return geode_stage_step_index(flags.data(), count, here, step);
}

JNIEXPORT jint JNICALL
Java_dev_geode_engine_bridge_GeodeNative_stageStepMilk(JNIEnv* env, jobject, jobjectArray paths, jstring current,
                                                       jint step) {
    if (paths == nullptr) return -1;
    const jsize count = env->GetArrayLength(paths);
    std::vector<std::string> texts;
    texts.reserve(static_cast<size_t>(count));
    for (jsize i = 0; i < count; ++i) {
        auto path = static_cast<jstring>(env->GetObjectArrayElement(paths, i));
        const geode::jni::Utf8Chars chars(env, path);
        texts.emplace_back(chars.get() != nullptr ? chars.get() : "");
        env->DeleteLocalRef(path);
    }
    std::vector<const char*> ptrs;
    ptrs.reserve(texts.size());
    for (const auto& t : texts) ptrs.push_back(t.c_str());
    const geode::jni::Utf8Chars on(env, current);
    return geode_stage_step_milk(ptrs.data(), count, on.get() != nullptr ? on.get() : "", step);
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

JNIEXPORT jlong JNICALL
Java_dev_geode_engine_bridge_GeodeNative_surfaceSettleMs(JNIEnv*, jobject, jint previousMode, jint mode) {
    return geode_surface_settle_ms(previousMode, mode);
}

JNIEXPORT jint JNICALL
Java_dev_geode_engine_bridge_GeodeNative_surfaceFpsChoice(JNIEnv*, jobject, jint requestedFps) {
    return geode_surface_fps_choice(requestedFps);
}

JNIEXPORT void JNICALL
Java_dev_geode_engine_bridge_GeodeNative_autoTimerRestart(JNIEnv*, jobject, jint timer, jlong nowMs) {
    geode_auto_timer_restart(timer, nowMs);
}

JNIEXPORT jboolean JNICALL
Java_dev_geode_engine_bridge_GeodeNative_autoTimerDue(JNIEnv*, jobject, jint timer, jlong nowMs, jint intervalSec, jboolean running) {
    return geode_auto_timer_due(timer, nowMs, intervalSec, running == JNI_TRUE ? 1 : 0) ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT void JNICALL
Java_dev_geode_engine_bridge_GeodeNative_autoMarkChange(JNIEnv* env, jobject, jstring key) {
    if (!key) return;
    const char* chars = env->GetStringUTFChars(key, nullptr);
    if (!chars) return;
    geode_auto_mark_change(chars);
    env->ReleaseStringUTFChars(key, chars);
}

JNIEXPORT jint JNICALL
Java_dev_geode_engine_bridge_GeodeNative_autoPick(JNIEnv*, jobject, jint count, jint onScreen) {
    return geode_auto_pick(count, onScreen);
}

}  // extern "C"
