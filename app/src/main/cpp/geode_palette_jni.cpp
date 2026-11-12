// JNI for the look's palette stack (core/viz/Palette through api/geode_api.h): the stack as opaque text, its
// edits, the swatches Customize › Look draws and the stack every renderer of the process reads. Marshals only.
#include <jni.h>

#include <algorithm>
#include <vector>

#include "api/geode_api.h"
#include "jni_util.hpp"

namespace {

using geode::jni::Utf8Chars;
using geode::jni::utf8String;

// More than any swatch draws.
constexpr int kMaxSamples = 1024;

const char* textOf(const Utf8Chars& chars) { return chars.get() ? chars.get() : ""; }

template <typename Fill>
jintArray swatchOf(JNIEnv* env, jint samples, Fill fill) {
    const int count = std::clamp(static_cast<int>(samples), 0, kMaxSamples);
    std::vector<int> colours(static_cast<size_t>(count));
    if (count > 0) fill(count, colours.data());
    jintArray out = env->NewIntArray(count);
    if (out && count > 0) env->SetIntArrayRegion(out, 0, count, reinterpret_cast<const jint*>(colours.data()));
    return out;
}

}  // namespace

extern "C" {

JNIEXPORT jstring JNICALL
Java_dev_geode_engine_bridge_GeodeNative_paletteStackEditor(JNIEnv* env, jobject, jstring stack) {
    Utf8Chars text(env, stack);
    return utf8String(env, geode_palette_stack_editor(textOf(text)));
}

JNIEXPORT jstring JNICALL
Java_dev_geode_engine_bridge_GeodeNative_paletteStackNormalize(JNIEnv* env, jobject, jstring stack) {
    Utf8Chars text(env, stack);
    return utf8String(env, geode_palette_stack_normalize(textOf(text)));
}

JNIEXPORT jstring JNICALL
Java_dev_geode_engine_bridge_GeodeNative_paletteStackAddLibrary(JNIEnv* env, jobject, jstring stack, jint index) {
    Utf8Chars text(env, stack);
    return utf8String(env, geode_palette_stack_add_library(textOf(text), index));
}

JNIEXPORT jstring JNICALL
Java_dev_geode_engine_bridge_GeodeNative_paletteStackAddColours(JNIEnv* env, jobject, jstring stack, jstring name, jintArray rgb) {
    Utf8Chars text(env, stack);
    Utf8Chars label(env, name);
    const jsize count = rgb ? env->GetArrayLength(rgb) : 0;
    std::vector<jint> colours(static_cast<size_t>(count));
    if (count > 0) env->GetIntArrayRegion(rgb, 0, count, colours.data());
    return utf8String(env, geode_palette_stack_add_colours(textOf(text), textOf(label), reinterpret_cast<const int*>(colours.data()),
                                                           static_cast<int>(count)));
}

JNIEXPORT jstring JNICALL
Java_dev_geode_engine_bridge_GeodeNative_paletteStackRemove(JNIEnv* env, jobject, jstring stack, jint layer) {
    Utf8Chars text(env, stack);
    return utf8String(env, geode_palette_stack_remove(textOf(text), layer));
}

JNIEXPORT jstring JNICALL
Java_dev_geode_engine_bridge_GeodeNative_paletteStackMove(JNIEnv* env, jobject, jstring stack, jint layer, jint delta) {
    Utf8Chars text(env, stack);
    return utf8String(env, geode_palette_stack_move(textOf(text), layer, delta));
}

JNIEXPORT jstring JNICALL
Java_dev_geode_engine_bridge_GeodeNative_paletteStackSetBlend(JNIEnv* env, jobject, jstring stack, jstring blend) {
    Utf8Chars text(env, stack);
    Utf8Chars name(env, blend);
    return utf8String(env, geode_palette_stack_set_blend(textOf(text), textOf(name)));
}

JNIEXPORT jintArray JNICALL
Java_dev_geode_engine_bridge_GeodeNative_paletteStackLayerSwatch(JNIEnv* env, jobject, jstring stack, jint layer, jint samples) {
    Utf8Chars text(env, stack);
    return swatchOf(env, samples, [&](int count, int* out) { geode_palette_stack_layer_swatch(textOf(text), layer, count, out); });
}

JNIEXPORT jintArray JNICALL
Java_dev_geode_engine_bridge_GeodeNative_paletteLibrarySwatch(JNIEnv* env, jobject, jint index, jint samples) {
    return swatchOf(env, samples, [&](int count, int* out) { geode_palette_library_swatch(index, count, out); });
}

JNIEXPORT void JNICALL
Java_dev_geode_engine_bridge_GeodeNative_paletteStackSet(JNIEnv* env, jobject, jstring stack) {
    Utf8Chars text(env, stack);
    geode_palette_stack_set(textOf(text));
}

}  // extern "C"
