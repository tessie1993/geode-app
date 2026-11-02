#include <jni.h>

#include <algorithm>
#include <vector>

#include "api/geode_api.h"

// The Studio preview clock (core/studio/PreviewClock): values in, values out, nothing else.

namespace {
geode_preview* previewOf(jlong handle) { return reinterpret_cast<geode_preview*>(handle); }
}  // namespace

extern "C" {

JNIEXPORT jlong JNICALL Java_dev_geode_engine_bridge_GeodeNative_previewCreate(JNIEnv*, jobject) {
    return reinterpret_cast<jlong>(geode_preview_create());
}

JNIEXPORT void JNICALL Java_dev_geode_engine_bridge_GeodeNative_previewDestroy(JNIEnv*, jobject, jlong handle) {
    if (handle) geode_preview_destroy(previewOf(handle));
}

JNIEXPORT void JNICALL Java_dev_geode_engine_bridge_GeodeNative_previewSetSpans(JNIEnv* env, jobject, jlong handle,
                                                                                jlongArray starts, jlongArray durations) {
    if (!handle) return;
    const jsize n = (starts && durations) ? std::min(env->GetArrayLength(starts), env->GetArrayLength(durations)) : 0;
    std::vector<long long> s(static_cast<size_t>(n));
    std::vector<long long> d(static_cast<size_t>(n));
    std::vector<jlong> buf(static_cast<size_t>(n));
    if (n > 0) {
        env->GetLongArrayRegion(starts, 0, n, buf.data());
        for (jsize i = 0; i < n; ++i) s[static_cast<size_t>(i)] = buf[static_cast<size_t>(i)];
        env->GetLongArrayRegion(durations, 0, n, buf.data());
        for (jsize i = 0; i < n; ++i) d[static_cast<size_t>(i)] = buf[static_cast<size_t>(i)];
    }
    geode_preview_set_spans(previewOf(handle), s.data(), d.data(), static_cast<int>(n));
}

JNIEXPORT void JNICALL Java_dev_geode_engine_bridge_GeodeNative_previewSetRate(JNIEnv*, jobject, jlong handle, jint fps,
                                                                               jlong lengthMs) {
    if (handle) geode_preview_set_rate(previewOf(handle), fps, lengthMs);
}

JNIEXPORT void JNICALL Java_dev_geode_engine_bridge_GeodeNative_previewPlay(JNIEnv*, jobject, jlong handle,
                                                                            jlong programmeMs, jlong nowNs) {
    if (handle) geode_preview_play(previewOf(handle), programmeMs, nowNs);
}

JNIEXPORT void JNICALL Java_dev_geode_engine_bridge_GeodeNative_previewPause(JNIEnv*, jobject, jlong handle, jlong nowNs) {
    if (handle) geode_preview_pause(previewOf(handle), nowNs);
}

JNIEXPORT void JNICALL Java_dev_geode_engine_bridge_GeodeNative_previewSeek(JNIEnv*, jobject, jlong handle,
                                                                            jlong programmeMs, jlong nowNs) {
    if (handle) geode_preview_seek(previewOf(handle), programmeMs, nowNs);
}

JNIEXPORT void JNICALL Java_dev_geode_engine_bridge_GeodeNative_previewAudio(JNIEnv*, jobject, jlong handle, jint span,
                                                                             jlong spanPosMs, jlong nowNs) {
    if (handle) geode_preview_audio(previewOf(handle), span, spanPosMs, nowNs);
}

JNIEXPORT jlong JNICALL Java_dev_geode_engine_bridge_GeodeNative_previewNow(JNIEnv*, jobject, jlong handle, jlong nowNs) {
    return handle ? geode_preview_now(previewOf(handle), nowNs) : 0;
}

JNIEXPORT jboolean JNICALL Java_dev_geode_engine_bridge_GeodeNative_previewEnded(JNIEnv*, jobject, jlong handle,
                                                                                 jlong nowNs) {
    return (handle && geode_preview_ended(previewOf(handle), nowNs)) ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jint JNICALL Java_dev_geode_engine_bridge_GeodeNative_previewLocate(JNIEnv* env, jobject, jlong handle,
                                                                              jlong programmeMs, jlongArray offsetOut) {
    if (!handle) return -1;
    long long offset = 0;
    const int span = geode_preview_locate(previewOf(handle), programmeMs, &offset);
    if (offsetOut && env->GetArrayLength(offsetOut) > 0) {
        const jlong value = offset;
        env->SetLongArrayRegion(offsetOut, 0, 1, &value);
    }
    return span;
}

JNIEXPORT jlong JNICALL Java_dev_geode_engine_bridge_GeodeNative_previewFrameOf(JNIEnv*, jobject, jlong programmeMs,
                                                                                jint fps) {
    return geode_preview_frame_of(programmeMs, fps);
}

}  // extern "C"
