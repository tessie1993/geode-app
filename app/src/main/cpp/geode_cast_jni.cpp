// The cast's HTTP server (core/cast/LiveServer through geode_cast_server_*) and its encoders (geode_cast_encoder_*):
// marshalling only.
#include <android/native_window.h>
#include <android/native_window_jni.h>
#include <jni.h>

#include <cstdint>

#include "api/geode_api.h"
#include "jni_util.hpp"

extern "C" {

JNIEXPORT jlong JNICALL
Java_dev_geode_engine_bridge_CastNative_castServerOpen(JNIEnv* env, jobject, jstring boundDotted, jint bound, jint prefix,
                                                        jlong webrtc) {
    geode::jni::Utf8Chars b(env, boundDotted);
    return static_cast<jlong>(geode_cast_server_open(b.get(), static_cast<uint32_t>(bound), prefix, webrtc));
}

JNIEXPORT jint JNICALL
Java_dev_geode_engine_bridge_CastNative_castServerStart(JNIEnv*, jobject, jlong handle) {
    return geode_cast_server_start(handle);
}

JNIEXPORT void JNICALL
Java_dev_geode_engine_bridge_CastNative_castServerStop(JNIEnv*, jobject, jlong handle) {
    geode_cast_server_stop(handle);
}

JNIEXPORT void JNICALL
Java_dev_geode_engine_bridge_CastNative_castServerClose(JNIEnv*, jobject, jlong handle) {
    geode_cast_server_close(handle);
}

JNIEXPORT void JNICALL
Java_dev_geode_engine_bridge_CastNative_castServerAdmit(JNIEnv*, jobject, jlong handle, jint tv, jint wants) {
    geode_cast_server_admit(handle, static_cast<uint32_t>(tv), wants);
}

JNIEXPORT jlong JNICALL
Java_dev_geode_engine_bridge_CastNative_castEncoderOpen(JNIEnv*, jobject, jlong server, jlong webrtc, jlong music) {
    return reinterpret_cast<jlong>(
        geode_cast_encoder_open(server, webrtc, reinterpret_cast<const geode_pcm_ring*>(music)));
}

// A Surface of the encoder's input window; it holds its own reference, so the caller releases it when done.
JNIEXPORT jobject JNICALL
Java_dev_geode_engine_bridge_CastNative_castEncoderSurface(JNIEnv* env, jobject, jlong handle) {
    void* window = geode_cast_encoder_window(reinterpret_cast<const geode_cast_encoder*>(handle));
    return window ? ANativeWindow_toSurface(env, static_cast<ANativeWindow*>(window)) : nullptr;
}

JNIEXPORT void JNICALL
Java_dev_geode_engine_bridge_CastNative_castEncoderPump(JNIEnv*, jobject, jlong handle, jlong current, jlong untilUs) {
    geode_cast_encoder_pump(reinterpret_cast<geode_cast_encoder*>(handle),
                            reinterpret_cast<const geode_pcm_ring*>(current), untilUs);
}

JNIEXPORT jboolean JNICALL
Java_dev_geode_engine_bridge_CastNative_castEncoderFailed(JNIEnv*, jobject, jlong handle) {
    return geode_cast_encoder_failed(reinterpret_cast<const geode_cast_encoder*>(handle)) ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT void JNICALL
Java_dev_geode_engine_bridge_CastNative_castEncoderClose(JNIEnv*, jobject, jlong handle) {
    geode_cast_encoder_close(reinterpret_cast<geode_cast_encoder*>(handle));
}

JNIEXPORT jint JNICALL
Java_dev_geode_engine_bridge_CastNative_castWatch(JNIEnv*, jobject, jboolean announced, jlong nowMs, jlong waitingSinceMs,
                                                   jboolean playlistReady, jlong segmentAgeMs, jboolean addressHere) {
    return geode_cast_watch(announced ? 1 : 0, nowMs, waitingSinceMs, playlistReady ? 1 : 0, segmentAgeMs,
                            addressHere ? 1 : 0);
}

JNIEXPORT jlong JNICALL
Java_dev_geode_engine_bridge_CastNative_castWatchNextMs(JNIEnv*, jobject, jboolean announced) {
    return geode_cast_watch_next_ms(announced ? 1 : 0);
}

JNIEXPORT jboolean JNICALL
Java_dev_geode_engine_bridge_CastNative_castFallbackRelease(JNIEnv*, jobject, jlong nowNs, jlong drewNs) {
    return geode_cast_fallback_release(nowNs, drewNs) ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jstring JNICALL
Java_dev_geode_engine_bridge_CastNative_castServerText(JNIEnv* env, jobject, jlong handle, jint which) {
    char out[GEODE_CAST_TEXT_CAP];
    geode_cast_server_text(handle, which, out, GEODE_CAST_TEXT_CAP);
    return geode::jni::utf8String(env, out);
}

JNIEXPORT void JNICALL
Java_dev_geode_engine_bridge_CastNative_castServerState(JNIEnv* env, jobject, jlong handle, jlongArray out) {
    if (!out || env->GetArrayLength(out) < GEODE_CAST_STATE) return;
    int64_t values[GEODE_CAST_STATE];
    geode_cast_server_state(handle, values);
    env->SetLongArrayRegion(out, 0, GEODE_CAST_STATE, reinterpret_cast<const jlong*>(values));
}

}  // extern "C"
