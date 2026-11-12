// JNI for the look files (core/viz/LookFiles through api/geode_api.h): file names, saved palettes, presets and the
// template library as text in and text out. Marshals only.
#include <jni.h>

#include <string>
#include <vector>

#include "api/geode_api.h"
#include "jni_util.hpp"

namespace {

using geode::jni::FloatElements;
using geode::jni::Utf8Chars;
using geode::jni::utf8String;

const char* textOf(const Utf8Chars& chars) { return chars.get() ? chars.get() : ""; }

jintArray intsOf(JNIEnv* env, const int* values, int count) {
    const jsize n = count > 0 ? static_cast<jsize>(count) : 0;
    jintArray out = env->NewIntArray(n);
    if (out && n > 0) env->SetIntArrayRegion(out, 0, n, reinterpret_cast<const jint*>(values));
    return out;
}

// The Java strings of `array` as UTF-8, null entries as "".
std::vector<std::string> stringsOf(JNIEnv* env, jobjectArray array) {
    std::vector<std::string> out;
    const jsize n = array ? env->GetArrayLength(array) : 0;
    for (jsize i = 0; i < n; ++i) {
        auto* item = static_cast<jstring>(env->GetObjectArrayElement(array, i));
        Utf8Chars chars(env, item);
        out.emplace_back(textOf(chars));
        env->DeleteLocalRef(item);
    }
    return out;
}

}  // namespace

extern "C" {

JNIEXPORT jstring JNICALL Java_dev_geode_engine_bridge_GeodeNative_fileSafeName(JNIEnv* env, jobject, jstring name) {
    Utf8Chars text(env, name);
    return utf8String(env, geode_file_safe_name(textOf(text)));
}

JNIEXPORT jstring JNICALL Java_dev_geode_engine_bridge_GeodeNative_fileSafeChars(JNIEnv* env, jobject, jstring name) {
    Utf8Chars text(env, name);
    return utf8String(env, geode_file_safe_chars(textOf(text)));
}

JNIEXPORT jintArray JNICALL
Java_dev_geode_engine_bridge_GeodeNative_looksOrder(JNIEnv* env, jobject, jobjectArray names, jboolean caseless) {
    const std::vector<std::string> list = stringsOf(env, names);
    std::vector<const char*> pointers;
    for (const auto& s : list) pointers.push_back(s.c_str());
    std::vector<int> order(list.size());
    const int n = geode_looks_order(pointers.data(), static_cast<int>(pointers.size()), caseless ? 1 : 0, order.data());
    return intsOf(env, order.data(), n);
}

JNIEXPORT jstring JNICALL Java_dev_geode_engine_bridge_GeodeNative_savedPaletteRead(JNIEnv* env, jobject, jstring text) {
    Utf8Chars chars(env, text);
    return utf8String(env, geode_saved_palette_read(textOf(chars)));
}

JNIEXPORT jstring JNICALL Java_dev_geode_engine_bridge_GeodeNative_savedPaletteSanitize(JNIEnv* env, jobject, jstring text) {
    Utf8Chars chars(env, text);
    return utf8String(env, geode_saved_palette_sanitize(textOf(chars)));
}

JNIEXPORT jstring JNICALL Java_dev_geode_engine_bridge_GeodeNative_savedPaletteCreate(JNIEnv* env, jobject, jstring name, jfloat base,
                                                                                    jfloat span, jintArray stops) {
    Utf8Chars label(env, name);
    const jsize n = stops ? env->GetArrayLength(stops) : 0;
    std::vector<jint> rgb(static_cast<size_t>(n));
    if (n > 0) env->GetIntArrayRegion(stops, 0, n, rgb.data());
    return utf8String(env, geode_saved_palette_create(textOf(label), base, span, reinterpret_cast<const int*>(rgb.data()), static_cast<int>(n)));
}

JNIEXPORT jstring JNICALL Java_dev_geode_engine_bridge_GeodeNative_savedPaletteMigrate(JNIEnv* env, jobject, jstring text) {
    Utf8Chars chars(env, text);
    return utf8String(env, geode_saved_palette_migrate(textOf(chars)));
}

JNIEXPORT jstring JNICALL Java_dev_geode_engine_bridge_GeodeNative_savedPaletteIdFor(JNIEnv* env, jobject, jstring name) {
    Utf8Chars chars(env, name);
    return utf8String(env, geode_saved_palette_id_for(textOf(chars)));
}

JNIEXPORT jstring JNICALL
Java_dev_geode_engine_bridge_GeodeNative_savedPaletteString(JNIEnv* env, jobject, jstring text, jstring key) {
    Utf8Chars chars(env, text);
    Utf8Chars which(env, key);
    return utf8String(env, geode_saved_palette_string(textOf(chars), textOf(which)));
}

JNIEXPORT jfloatArray JNICALL Java_dev_geode_engine_bridge_GeodeNative_savedPaletteHues(JNIEnv* env, jobject, jstring text) {
    Utf8Chars chars(env, text);
    float hues[2] = {0.0f, 1.0f};
    geode_saved_palette_hues(textOf(chars), hues);
    jfloatArray out = env->NewFloatArray(2);
    if (out) env->SetFloatArrayRegion(out, 0, 2, hues);
    return out;
}

JNIEXPORT jintArray JNICALL Java_dev_geode_engine_bridge_GeodeNative_savedPaletteStops(JNIEnv* env, jobject, jstring text) {
    Utf8Chars chars(env, text);
    std::vector<int> stops(static_cast<size_t>(geode_saved_palette_max_stops()));
    const int n = geode_saved_palette_stops(textOf(chars), stops.data());
    return intsOf(env, stops.data(), n);
}

JNIEXPORT jint JNICALL Java_dev_geode_engine_bridge_GeodeNative_savedPaletteMaxStops(JNIEnv*, jobject) {
    return geode_saved_palette_max_stops();
}

JNIEXPORT jfloatArray JNICALL
Java_dev_geode_engine_bridge_GeodeNative_savedPaletteApply(JNIEnv* env, jobject, jstring text, jfloatArray frame) {
    Utf8Chars chars(env, text);
    FloatElements values(env, frame);
    const jsize count = static_cast<jsize>(geode_viz_param_count());
    if (!values.get() || values.length() < count) return frame;
    std::vector<float> out(static_cast<size_t>(count));
    geode_saved_palette_apply(textOf(chars), values.get(), out.data());
    jfloatArray result = env->NewFloatArray(count);
    if (result) env->SetFloatArrayRegion(result, 0, count, out.data());
    return result;
}

JNIEXPORT jstring JNICALL
Java_dev_geode_engine_bridge_GeodeNative_savedPaletteKeptId(JNIEnv* env, jobject, jstring lookId, jstring deletedId) {
    Utf8Chars look(env, lookId);
    Utf8Chars deleted(env, deletedId);
    return utf8String(env, geode_saved_palette_kept_id(textOf(look), textOf(deleted)));
}

JNIEXPORT jboolean JNICALL Java_dev_geode_engine_bridge_GeodeNative_presetRead(JNIEnv* env, jobject, jstring text, jfloatArray frame,
                                                                             jobjectArray strings, jfloatArray envelope) {
    const jsize count = static_cast<jsize>(geode_viz_param_count());
    if (!frame || env->GetArrayLength(frame) < count || !strings || env->GetArrayLength(strings) < 5 || !envelope ||
        env->GetArrayLength(envelope) < 2) {
        return JNI_FALSE;
    }
    Utf8Chars chars(env, text);
    std::vector<float> values(static_cast<size_t>(count));
    const char* read[5] = {};
    float pair[2] = {};
    if (!geode_preset_read(textOf(chars), values.data(), count, read, pair)) return JNI_FALSE;
    env->SetFloatArrayRegion(frame, 0, count, values.data());
    env->SetFloatArrayRegion(envelope, 0, 2, pair);
    for (jsize i = 0; i < 5; ++i) {
        jstring s = utf8String(env, read[i]);
        if (env->ExceptionCheck()) return JNI_FALSE;
        env->SetObjectArrayElement(strings, i, s);
        if (s) env->DeleteLocalRef(s);
    }
    return JNI_TRUE;
}

JNIEXPORT jstring JNICALL Java_dev_geode_engine_bridge_GeodeNative_presetWrite(JNIEnv* env, jobject, jstring name, jstring sceneId,
                                                                             jfloat attack, jfloat decay, jstring customShader,
                                                                             jstring milkPreset, jfloatArray frame,
                                                                             jstring customPaletteId) {
    Utf8Chars label(env, name);
    Utf8Chars scene(env, sceneId);
    Utf8Chars shader(env, customShader);
    Utf8Chars milk(env, milkPreset);
    Utf8Chars paletteId(env, customPaletteId);
    FloatElements values(env, frame);
    if (!values.get()) return nullptr;
    return utf8String(env, geode_preset_write(textOf(label), textOf(scene), attack, decay, shader.get(), milk.get(), values.get(),
                                              values.length(), textOf(paletteId)));
}

JNIEXPORT jstring JNICALL Java_dev_geode_engine_bridge_GeodeNative_presetMilkFileName(JNIEnv* env, jobject, jstring presetName) {
    Utf8Chars chars(env, presetName);
    return utf8String(env, geode_preset_milk_file_name(textOf(chars)));
}

JNIEXPORT jstring JNICALL
Java_dev_geode_engine_bridge_GeodeNative_templateFileName(JNIEnv* env, jobject, jstring name, jstring id, jstring suffix) {
    Utf8Chars label(env, name);
    Utf8Chars ident(env, id);
    Utf8Chars end(env, suffix);
    return utf8String(env, geode_template_file_name(textOf(label), textOf(ident), textOf(end)));
}

JNIEXPORT jstring JNICALL Java_dev_geode_engine_bridge_GeodeNative_templateFileTail(JNIEnv* env, jobject, jstring id, jstring suffix) {
    Utf8Chars ident(env, id);
    Utf8Chars end(env, suffix);
    return utf8String(env, geode_template_file_tail(textOf(ident), textOf(end)));
}

JNIEXPORT jstring JNICALL
Java_dev_geode_engine_bridge_GeodeNative_templateUniqueName(JNIEnv* env, jobject, jstring base, jobjectArray taken) {
    Utf8Chars stem(env, base);
    const std::vector<std::string> names = stringsOf(env, taken);
    std::vector<const char*> pointers;
    for (const auto& s : names) pointers.push_back(s.c_str());
    return utf8String(env, geode_template_unique_name(textOf(stem), pointers.data(), static_cast<int>(pointers.size())));
}

JNIEXPORT jint JNICALL Java_dev_geode_engine_bridge_GeodeNative_templateStarterCount(JNIEnv*, jobject) {
    return geode_template_starter_count();
}

JNIEXPORT jstring JNICALL Java_dev_geode_engine_bridge_GeodeNative_templateStarter(JNIEnv* env, jobject, jint index) {
    return utf8String(env, geode_template_starter(index));
}

JNIEXPORT jboolean JNICALL Java_dev_geode_engine_bridge_GeodeNative_templateIsStarter(JNIEnv* env, jobject, jstring id) {
    Utf8Chars ident(env, id);
    return geode_template_is_starter(textOf(ident)) ? JNI_TRUE : JNI_FALSE;
}

}
