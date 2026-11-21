// The cast's HTTP server (core/cast/LiveServer through geode_cast_server_*) and its render thread, which owns the
// encoders (geode_cast_render_*): marshalling only.
#include <android/asset_manager_jni.h>
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

// The cast's render thread (core/output/android/CastRenderHost): it opens the encoders, the EGL context, the tee and the
// fallback renderer itself; the shell hands it the stream's handles and the look, and polls it.
JNIEXPORT jlong JNICALL
Java_dev_geode_engine_bridge_CastNative_castRenderStart(JNIEnv* env, jobject, jlong server, jlong webrtc, jlong music,
                                                         jobject assetManager, jstring cacheDir) {
    geode::jni::Utf8Chars dir(env, cacheDir);
    AAssetManager* assets = assetManager ? AAssetManager_fromJava(env, assetManager) : nullptr;
    return reinterpret_cast<jlong>(
        geode_cast_render_start(server, webrtc, reinterpret_cast<const geode_pcm_ring*>(music), assets, dir.get()));
}

JNIEXPORT void JNICALL
Java_dev_geode_engine_bridge_CastNative_castRenderSetLook(JNIEnv* env, jobject, jlong handle, jstring sceneId,
                                                           jfloatArray params, jstring lfos, jstring adsrs,
                                                           jfloatArray tuning, jstring reactions, jboolean reducedMotion,
                                                           jstring transitionId, jlong transitionMs) {
    geode::jni::LookArgs look(env, sceneId, params, lfos, adsrs, tuning, reactions, reducedMotion, transitionId, transitionMs);
    if (look.ok()) geode_cast_render_set_look(reinterpret_cast<geode_cast_render*>(handle), look.get());
}

JNIEXPORT jboolean JNICALL
Java_dev_geode_engine_bridge_CastNative_castRenderStop(JNIEnv*, jobject, jlong handle, jlong timeoutMs) {
    return geode_cast_render_stop(reinterpret_cast<geode_cast_render*>(handle), timeoutMs) ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jboolean JNICALL
Java_dev_geode_engine_bridge_CastNative_castRenderFailed(JNIEnv*, jobject, jlong handle) {
    return geode_cast_render_failed(reinterpret_cast<const geode_cast_render*>(handle)) ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jint JNICALL
Java_dev_geode_engine_bridge_CastNative_castRenderTakeDirectorMoments(JNIEnv*, jobject, jlong handle) {
    return geode_cast_render_take_director_moments(reinterpret_cast<geode_cast_render*>(handle));
}

JNIEXPORT void JNICALL
Java_dev_geode_engine_bridge_CastNative_castRenderFree(JNIEnv*, jobject, jlong handle) {
    geode_cast_render_free(reinterpret_cast<geode_cast_render*>(handle));
}

JNIEXPORT void JNICALL
Java_dev_geode_engine_bridge_CastNative_castRenderSetStageFlags(JNIEnv*, jobject, jint flags) {
    geode_cast_render_set_stage_flags(static_cast<unsigned>(flags));
}

JNIEXPORT void JNICALL
Java_dev_geode_engine_bridge_CastNative_castRenderSetDeviceRing(JNIEnv*, jobject, jlong ring) {
    geode_cast_render_set_device_ring(reinterpret_cast<const geode_pcm_ring*>(ring));
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
