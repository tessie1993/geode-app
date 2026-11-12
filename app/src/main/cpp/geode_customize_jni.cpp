// JNI for what Customize draws (core/viz/CustomizeRows, PanelText, StyleNames, PalettePanel through api/geode_api.h):
// the control rows, the panel's words, the style names and the palette pages. Marshals only.
#include <jni.h>

#include <algorithm>
#include <vector>

#include "api/geode_api.h"
#include "jni_util.hpp"

namespace {

using geode::jni::FloatElements;
using geode::jni::Utf8Chars;
using geode::jni::utf8String;

// More than any palette holds.
constexpr int kMaxColours = 64;

const char* textOf(const Utf8Chars& chars) { return chars.get() ? chars.get() : ""; }

// A params wire long enough for the engine, else null: the defaults.
const float* wireOf(const FloatElements& params) { return params.length() >= geode_viz_param_count() ? params.get() : nullptr; }

std::vector<int> intsOf(JNIEnv* env, jintArray array) {
    const jsize n = array ? std::min<jsize>(env->GetArrayLength(array), kMaxColours) : 0;
    std::vector<int> out(static_cast<size_t>(n));
    if (n > 0) env->GetIntArrayRegion(array, 0, n, reinterpret_cast<jint*>(out.data()));
    return out;
}

jintArray toJava(JNIEnv* env, const int* values, int count) {
    jintArray out = env->NewIntArray(count);
    if (out && count > 0) env->SetIntArrayRegion(out, 0, count, reinterpret_cast<const jint*>(values));
    return out;
}

}  // namespace

extern "C" {

JNIEXPORT jstring JNICALL
Java_dev_geode_engine_bridge_GeodeNative_customizeRows(JNIEnv* env, jobject, jstring styleId, jstring keys, jstring query,
                                                       jboolean transition, jint liquidMode, jfloatArray params) {
    Utf8Chars style(env, styleId);
    Utf8Chars keyLines(env, keys);
    Utf8Chars search(env, query);
    FloatElements look(env, params);
    return utf8String(env, geode_customize_rows(textOf(style), textOf(keyLines), textOf(search), transition == JNI_TRUE ? 1 : 0,
                                                liquidMode, wireOf(look)));
}

JNIEXPORT jstring JNICALL
Java_dev_geode_engine_bridge_GeodeNative_panelText(JNIEnv* env, jobject, jstring id) {
    Utf8Chars text(env, id);
    return utf8String(env, geode_panel_text(textOf(text)));
}

JNIEXPORT jstring JNICALL
Java_dev_geode_engine_bridge_GeodeNative_styleLabel(JNIEnv* env, jobject, jstring styleId) {
    Utf8Chars id(env, styleId);
    return utf8String(env, geode_style_label(textOf(id)));
}

JNIEXPORT jstring JNICALL
Java_dev_geode_engine_bridge_GeodeNative_styleFamilyLabel(JNIEnv* env, jobject, jstring family) {
    Utf8Chars name(env, family);
    return utf8String(env, geode_style_family_label(textOf(name)));
}

JNIEXPORT jstring JNICALL
Java_dev_geode_engine_bridge_GeodeNative_paletteMakerStart(JNIEnv* env, jobject, jfloatArray params) {
    FloatElements look(env, params);
    return utf8String(env, geode_palette_maker_start(wireOf(look)));
}

JNIEXPORT jstring JNICALL
Java_dev_geode_engine_bridge_GeodeNative_paletteMakerFromLook(JNIEnv* env, jobject, jfloatArray params) {
    FloatElements look(env, params);
    return utf8String(env, geode_palette_maker_from_look(wireOf(look)));
}

JNIEXPORT jstring JNICALL
Java_dev_geode_engine_bridge_GeodeNative_paletteMakerOpen(JNIEnv* env, jobject, jintArray stops, jfloat base, jfloat span) {
    const std::vector<int> in = intsOf(env, stops);
    return utf8String(env, geode_palette_maker_open(in.data(), static_cast<int>(in.size()), base, span));
}

JNIEXPORT jstring JNICALL
Java_dev_geode_engine_bridge_GeodeNative_paletteMakerEdit(JNIEnv* env, jobject, jstring state, jint op, jint arg) {
    Utf8Chars text(env, state);
    return utf8String(env, geode_palette_maker_edit(textOf(text), op, arg));
}

JNIEXPORT jintArray JNICALL
Java_dev_geode_engine_bridge_GeodeNative_paletteMakerStops(JNIEnv* env, jobject, jstring state) {
    Utf8Chars text(env, state);
    int out[kMaxColours] = {};
    return toJava(env, out, geode_palette_maker_stops(textOf(text), out, kMaxColours));
}

JNIEXPORT jstring JNICALL
Java_dev_geode_engine_bridge_GeodeNative_paletteMakerView(JNIEnv* env, jobject, jstring state) {
    Utf8Chars text(env, state);
    return utf8String(env, geode_palette_maker_view(textOf(text)));
}

JNIEXPORT jintArray JNICALL
Java_dev_geode_engine_bridge_GeodeNative_paletteSavedColours(JNIEnv* env, jobject, jintArray stops, jfloat base, jfloat span) {
    const std::vector<int> in = intsOf(env, stops);
    int out[kMaxColours] = {};
    return toJava(env, out, geode_palette_saved_colours(in.data(), static_cast<int>(in.size()), base, span, out, kMaxColours));
}

JNIEXPORT jstring JNICALL
Java_dev_geode_engine_bridge_GeodeNative_paletteSavedSwatch(JNIEnv* env, jobject, jintArray stops, jfloat base, jfloat span) {
    const std::vector<int> in = intsOf(env, stops);
    return utf8String(env, geode_palette_saved_swatch(in.data(), static_cast<int>(in.size()), base, span));
}

JNIEXPORT jstring JNICALL
Java_dev_geode_engine_bridge_GeodeNative_paletteListView(JNIEnv* env, jobject, jfloatArray params, jstring customId, jstring savedIds,
                                                         jstring savedNames) {
    FloatElements look(env, params);
    Utf8Chars custom(env, customId);
    Utf8Chars ids(env, savedIds);
    Utf8Chars names(env, savedNames);
    return utf8String(env, geode_palette_list_view(wireOf(look), textOf(custom), textOf(ids), textOf(names)));
}

JNIEXPORT jstring JNICALL
Java_dev_geode_engine_bridge_GeodeNative_paletteStackCard(JNIEnv* env, jobject, jstring stack, jfloatArray params, jint savedCount) {
    Utf8Chars text(env, stack);
    FloatElements look(env, params);
    return utf8String(env, geode_palette_stack_card(textOf(text), wireOf(look), savedCount));
}

}  // extern "C"
