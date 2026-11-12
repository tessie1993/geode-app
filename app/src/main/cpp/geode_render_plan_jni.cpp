// JNI for a render's plan (core/api/geode_render_plan_api.h): marshalling only.
#include <jni.h>

#include <cstdint>
#include <string>
#include <vector>

#include "api/geode_render_plan_api.h"
#include "jni_util.hpp"

namespace {

// The leading `count` of (start ms, duration ms, end ms) into a long[]: as many as it has room for, none when it
// holds fewer than two.
void putRange(JNIEnv* env, jlongArray out, const jlong* values, jsize count) {
    if (!out) return;
    const jsize room = env->GetArrayLength(out);
    if (room < 2) return;
    env->SetLongArrayRegion(out, 0, room < count ? room : count, values);
}

}  // namespace

extern "C" {

JNIEXPORT jboolean JNICALL Java_dev_geode_engine_bridge_GeodeNative_exportRangeOf(JNIEnv* env, jobject, jfloat start,
                                                                                 jfloat end, jlong trackMs,
                                                                                 jlongArray out) {
    int64_t startMs = 0;
    int64_t durationMs = 0;
    int64_t endMs = 0;
    if (!geode_export_range_of(start, end, trackMs, &startMs, &durationMs, &endMs)) return JNI_FALSE;
    const jlong values[3] = {startMs, durationMs, endMs};
    putRange(env, out, values, 3);
    return JNI_TRUE;
}

JNIEXPORT void JNICALL Java_dev_geode_engine_bridge_GeodeNative_exportRangeCapped(JNIEnv* env, jobject,
                                                                                 jlong startMs, jlong durationMs,
                                                                                 jlong capMs, jlongArray out) {
    int64_t start = 0;
    int64_t duration = 0;
    geode_export_range_capped(startMs, durationMs, capMs, &start, &duration);
    const jlong values[2] = {start, duration};
    putRange(env, out, values, 2);
}

JNIEXPORT jlong JNICALL Java_dev_geode_engine_bridge_GeodeNative_exportRangeMinMs(JNIEnv*, jobject) {
    return geode_export_range_min_ms();
}

JNIEXPORT jintArray JNICALL Java_dev_geode_engine_bridge_GeodeNative_renderBatchOrder(JNIEnv* env, jobject,
                                                                                    jobjectArray uris) {
    const jsize count = uris ? env->GetArrayLength(uris) : 0;
    std::vector<std::string> texts;
    texts.reserve(static_cast<size_t>(count));
    for (jsize i = 0; i < count; ++i) {
        auto* uri = static_cast<jstring>(env->GetObjectArrayElement(uris, i));
        {
            const geode::jni::Utf8Chars chars(env, uri);
            texts.emplace_back(chars.get() ? chars.get() : "");
        }
        env->DeleteLocalRef(uri);
        if (env->ExceptionCheck()) return nullptr;
    }
    std::vector<const char*> pointers;
    pointers.reserve(texts.size());
    for (const auto& text : texts) pointers.push_back(text.c_str());
    std::vector<int32_t> keep(texts.size());
    const int kept = geode_render_batch_order(pointers.data(), count, keep.data());
    jintArray result = env->NewIntArray(kept);
    if (result && kept > 0) env->SetIntArrayRegion(result, 0, kept, reinterpret_cast<const jint*>(keep.data()));
    return result;
}

JNIEXPORT jfloat JNICALL Java_dev_geode_engine_bridge_GeodeNative_renderBatchProgress(JNIEnv*, jobject, jint done,
                                                                                    jint total, jfloat within) {
    return geode_render_batch_progress(done, total, within);
}

}
