// JNI for Customize's dice and its undo history (core/viz/PanelRoll, core/viz/ParamHistory). Marshals only.
#include <jni.h>

#include <vector>

#include "api/geode_api.h"
#include "api/geode_modulation_api.h"
#include "jni_util.hpp"

namespace {

using geode::jni::Utf8Chars;
using geode::jni::utf8String;

bool frameFits(JNIEnv* env, jfloatArray array) { return array && env->GetArrayLength(array) >= geode_viz_param_count(); }

std::vector<float> frameOf(JNIEnv* env, jfloatArray array) {
    std::vector<float> frame(static_cast<size_t>(geode_viz_param_count()));
    env->GetFloatArrayRegion(array, 0, static_cast<jsize>(frame.size()), frame.data());
    return frame;
}

}  // namespace

extern "C" {

JNIEXPORT jint JNICALL
Java_dev_geode_engine_bridge_PanelNative_panelRoll(JNIEnv* env, jobject, jstring styleId, jstring category, jstring kept,
                                                   jboolean basicsOnly, jfloatArray in, jfloatArray out) {
    if (!frameFits(env, in) || !frameFits(env, out)) return 0;
    Utf8Chars style(env, styleId);
    Utf8Chars categoryName(env, category);
    Utf8Chars keys(env, kept);
    const std::vector<float> input = frameOf(env, in);
    std::vector<float> output(input.size());
    const int bits = geode_panel_roll(style.get(), categoryName.get(), keys.get(), basicsOnly ? 1 : 0, input.data(), output.data());
    env->SetFloatArrayRegion(out, 0, static_cast<jsize>(output.size()), output.data());
    return bits;
}

JNIEXPORT jint JNICALL
Java_dev_geode_engine_bridge_PanelNative_panelRollAll(JNIEnv* env, jobject, jstring styleId, jstring kept, jboolean basicsOnly,
                                                      jlong seed, jfloatArray before, jfloatArray base, jfloatArray out) {
    if (!frameFits(env, before) || !frameFits(env, base) || !frameFits(env, out)) return 0;
    Utf8Chars style(env, styleId);
    Utf8Chars keys(env, kept);
    const std::vector<float> was = frameOf(env, before);
    const std::vector<float> start = frameOf(env, base);
    std::vector<float> rolled(was.size());
    const int bits = geode_panel_roll_all(style.get(), keys.get(), basicsOnly ? 1 : 0, static_cast<uint64_t>(seed), was.data(),
                                          start.data(), rolled.data());
    env->SetFloatArrayRegion(out, 0, static_cast<jsize>(rolled.size()), rolled.data());
    return bits;
}

// Null when the roll changed nothing.
JNIEXPORT jstring JNICALL
Java_dev_geode_engine_bridge_PanelNative_modWavesRoll(JNIEnv* env, jobject, jstring styleId, jstring locked, jboolean all,
                                                      jfloatArray params, jstring lfos) {
    if (!frameFits(env, params)) return nullptr;
    Utf8Chars style(env, styleId);
    Utf8Chars keys(env, locked);
    Utf8Chars saved(env, lfos);
    const std::vector<float> look = frameOf(env, params);
    return utf8String(env, geode_mod_waves_roll(style.get(), keys.get(), all ? 1 : 0, look.data(), saved.get()));
}

JNIEXPORT void JNICALL
Java_dev_geode_engine_bridge_PanelNative_paramsRerollReactions(JNIEnv* env, jobject, jfloatArray in, jfloatArray out) {
    if (!frameFits(env, in) || !frameFits(env, out)) return;
    const std::vector<float> input = frameOf(env, in);
    std::vector<float> output(input.size());
    geode_params_reroll_reactions(input.data(), output.data());
    env->SetFloatArrayRegion(out, 0, static_cast<jsize>(output.size()), output.data());
}

JNIEXPORT jint JNICALL
Java_dev_geode_engine_bridge_PanelNative_paramHistoryRecord(JNIEnv* env, jobject, jfloatArray before, jstring palette, jlong nowMs,
                                                            jboolean discrete) {
    if (!frameFits(env, before)) return geode_param_history_state();
    Utf8Chars id(env, palette);
    const std::vector<float> was = frameOf(env, before);
    return geode_param_history_record(was.data(), static_cast<int>(was.size()), id.get(), nowMs, discrete ? 1 : 0);
}

JNIEXPORT jstring JNICALL
Java_dev_geode_engine_bridge_PanelNative_paramHistoryStep(JNIEnv* env, jobject, jboolean redo, jfloatArray current, jstring palette,
                                                          jfloatArray out) {
    if (!frameFits(env, current) || !frameFits(env, out)) return nullptr;
    Utf8Chars id(env, palette);
    const std::vector<float> now = frameOf(env, current);
    std::vector<float> back(now.size());
    const char* backId = geode_param_history_step(redo ? 1 : 0, now.data(), static_cast<int>(now.size()), id.get(), back.data());
    if (!backId) return nullptr;
    env->SetFloatArrayRegion(out, 0, static_cast<jsize>(back.size()), back.data());
    return utf8String(env, backId);
}

JNIEXPORT jint JNICALL
Java_dev_geode_engine_bridge_PanelNative_paramHistoryState(JNIEnv*, jobject) {
    return geode_param_history_state();
}

}  // extern "C"
