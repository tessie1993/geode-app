// Show on a computer over WebRTC (core/cast/WebRtcLink through geode_webrtc_*): marshalling only.
#include <jni.h>

#include <cstdint>
#include <vector>

#include "api/geode_api.h"
#include "jni_util.hpp"

extern "C" {

JNIEXPORT jlong JNICALL
Java_dev_geode_engine_bridge_GeodeNative_webrtcOpen(JNIEnv* env, jobject, jstring boundDotted, jint bound, jint prefix) {
    geode::jni::Utf8Chars b(env, boundDotted);
    return static_cast<jlong>(geode_webrtc_open(b.get(), static_cast<uint32_t>(bound), prefix));
}

JNIEXPORT void JNICALL
Java_dev_geode_engine_bridge_GeodeNative_webrtcClose(JNIEnv*, jobject, jlong handle) {
    geode_webrtc_close(handle);
}

JNIEXPORT jstring JNICALL
Java_dev_geode_engine_bridge_GeodeNative_webrtcAnswer(JNIEnv* env, jobject, jlong handle, jstring offer, jintArray result) {
    geode::jni::Utf8Chars o(env, offer);
    std::vector<char> out(GEODE_WEBRTC_ANSWER_CAP);
    const jint code = geode_webrtc_answer(handle, o.get(), out.data(), static_cast<int>(out.size()));
    if (result && env->GetArrayLength(result) >= 1) env->SetIntArrayRegion(result, 0, 1, &code);
    return code >= 0 ? geode::jni::utf8String(env, out.data()) : nullptr;
}

JNIEXPORT void JNICALL
Java_dev_geode_engine_bridge_GeodeNative_webrtcVideo(JNIEnv* env, jobject, jlong handle, jbyteArray data, jint size, jlong ptsUs,
                                                     jboolean key) {
    if (!data || size <= 0 || size > env->GetArrayLength(data)) return;
    jbyte* bytes = env->GetByteArrayElements(data, nullptr);
    if (!bytes) return;
    geode_webrtc_video(handle, reinterpret_cast<const uint8_t*>(bytes), static_cast<size_t>(size), ptsUs, key ? 1 : 0);
    env->ReleaseByteArrayElements(data, bytes, JNI_ABORT);
}

JNIEXPORT void JNICALL
Java_dev_geode_engine_bridge_GeodeNative_webrtcVideoConfig(JNIEnv* env, jobject, jlong handle, jbyteArray data, jint size) {
    if (!data || size <= 0 || size > env->GetArrayLength(data)) return;
    jbyte* bytes = env->GetByteArrayElements(data, nullptr);
    if (!bytes) return;
    geode_webrtc_video_config(handle, reinterpret_cast<const uint8_t*>(bytes), static_cast<size_t>(size));
    env->ReleaseByteArrayElements(data, bytes, JNI_ABORT);
}

JNIEXPORT void JNICALL
Java_dev_geode_engine_bridge_GeodeNative_webrtcAudio(JNIEnv* env, jobject, jlong handle, jbyteArray data, jint size, jlong ptsUs) {
    if (!data || size <= 0 || size > env->GetArrayLength(data)) return;
    jbyte* bytes = env->GetByteArrayElements(data, nullptr);
    if (!bytes) return;
    geode_webrtc_audio(handle, reinterpret_cast<const uint8_t*>(bytes), static_cast<size_t>(size), ptsUs);
    env->ReleaseByteArrayElements(data, bytes, JNI_ABORT);
}

JNIEXPORT jint JNICALL
Java_dev_geode_engine_bridge_GeodeNative_webrtcPeers(JNIEnv*, jobject, jlong handle) {
    return geode_webrtc_peers(handle);
}

}  // extern "C"
