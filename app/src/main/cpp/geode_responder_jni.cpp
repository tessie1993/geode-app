// JNI for the Event Responder (core/viz/modulation/Responder through api/geode_responder_api.h): Customize ›
// Music's rows and their responses as the saved reactions text. Marshals only. The two calls that need a
// renderer (the preview and the status) are in geode_viz_jni.cpp, beside the handle registry they need.
#include <jni.h>

#include <vector>

#include "api/geode_responder_api.h"
#include "jni_util.hpp"

namespace {

using geode::jni::Utf8Chars;
using geode::jni::utf8String;

// A text-out call's whole text; `call(out, capacity)` returns the length it needs.
template <typename Call>
jstring textOf(JNIEnv* env, Call call) {
    char fixed[2048];
    const int need = call(fixed, static_cast<int>(sizeof fixed));
    if (need < static_cast<int>(sizeof fixed)) return utf8String(env, fixed);
    std::vector<char> whole(static_cast<size_t>(need) + 1);
    call(whole.data(), need + 1);
    return utf8String(env, whole.data());
}

jfloatArray floatsOf(JNIEnv* env, const float* values, int count) {
    jfloatArray out = env->NewFloatArray(count);
    if (out && count > 0) env->SetFloatArrayRegion(out, 0, count, values);
    return out;
}

// The most one row can list: its user responses (24) and every route of the matrix (256) that sits on it.
constexpr int kMaxListed = 24 + GEODE_REACT_ROUTES;

}  // namespace

extern "C" {

JNIEXPORT jstring JNICALL
Java_dev_geode_engine_bridge_ResponderNative_responderDefaultText(JNIEnv* env, jobject) {
    return textOf(env, [&](char* out, int capacity) { return geode_resp_default_text(out, capacity); });
}

// Null for a row there is none of.
JNIEXPORT jstring JNICALL
Java_dev_geode_engine_bridge_ResponderNative_responderRowName(JNIEnv* env, jobject, jint row) {
    return utf8String(env, geode_resp_row_name(row));
}

JNIEXPORT jfloatArray JNICALL
Java_dev_geode_engine_bridge_ResponderNative_responderRows(JNIEnv* env, jobject, jstring reactions, jstring styleId, jint seed,
                                                           jint mask) {
    Utf8Chars saved(env, reactions);
    Utf8Chars id(env, styleId);
    float out[GEODE_RESP_ROWS * GEODE_RESP_ROW_VALUES] = {};
    return floatsOf(env, out,
                    geode_resp_rows(saved.get(), id.get(), seed, static_cast<unsigned>(mask), out, GEODE_RESP_ROWS * GEODE_RESP_ROW_VALUES));
}

JNIEXPORT jfloatArray JNICALL
Java_dev_geode_engine_bridge_ResponderNative_responderValues(JNIEnv* env, jobject, jstring reactions, jstring styleId, jint seed,
                                                             jint mask, jint row) {
    Utf8Chars saved(env, reactions);
    Utf8Chars id(env, styleId);
    std::vector<float> out(static_cast<size_t>(kMaxListed) * GEODE_RESP_LIST_VALUES);
    const int n = geode_resp_values(saved.get(), id.get(), seed, static_cast<unsigned>(mask), row, out.data(),
                                    static_cast<int>(out.size()));
    return floatsOf(env, out.data(), n);
}

JNIEXPORT jstring JNICALL
Java_dev_geode_engine_bridge_ResponderNative_responderEdit(JNIEnv* env, jobject, jstring reactions, jint op, jint row, jint index,
                                                           jint field, jfloat value) {
    Utf8Chars saved(env, reactions);
    return textOf(env, [&](char* out, int capacity) { return geode_resp_edit(saved.get(), op, row, index, field, value, out, capacity); });
}

}  // extern "C"
