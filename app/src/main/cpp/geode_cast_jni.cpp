// The cast's HTTP server (core/cast/LiveServer through geode_cast_server_*): marshalling only.
#include <jni.h>

#include <cstdint>

#include "api/geode_api.h"
#include "jni_util.hpp"

extern "C" {

JNIEXPORT jlong JNICALL
Java_dev_geode_engine_bridge_GeodeNative_castServerOpen(JNIEnv* env, jobject, jstring boundDotted, jint bound, jint prefix,
                                                        jlong webrtc) {
    geode::jni::Utf8Chars b(env, boundDotted);
    return static_cast<jlong>(geode_cast_server_open(b.get(), static_cast<uint32_t>(bound), prefix, webrtc));
}

JNIEXPORT jint JNICALL
Java_dev_geode_engine_bridge_GeodeNative_castServerStart(JNIEnv*, jobject, jlong handle) {
    return geode_cast_server_start(handle);
}

JNIEXPORT void JNICALL
Java_dev_geode_engine_bridge_GeodeNative_castServerStop(JNIEnv*, jobject, jlong handle) {
    geode_cast_server_stop(handle);
}

JNIEXPORT void JNICALL
Java_dev_geode_engine_bridge_GeodeNative_castServerClose(JNIEnv*, jobject, jlong handle) {
    geode_cast_server_close(handle);
}

JNIEXPORT void JNICALL
Java_dev_geode_engine_bridge_GeodeNative_castServerAdmit(JNIEnv*, jobject, jlong handle, jint tv, jint wants) {
    geode_cast_server_admit(handle, static_cast<uint32_t>(tv), wants);
}

JNIEXPORT void JNICALL
Java_dev_geode_engine_bridge_GeodeNative_castServerWrite(JNIEnv* env, jobject, jlong handle, jbyteArray data, jint size) {
    if (!data || size <= 0 || size > env->GetArrayLength(data)) return;
    jbyte* bytes = env->GetByteArrayElements(data, nullptr);
    if (!bytes) return;
    geode_cast_server_write(handle, reinterpret_cast<const uint8_t*>(bytes), static_cast<size_t>(size));
    env->ReleaseByteArrayElements(data, bytes, JNI_ABORT);
}

JNIEXPORT jstring JNICALL
Java_dev_geode_engine_bridge_GeodeNative_castServerText(JNIEnv* env, jobject, jlong handle, jint which) {
    char out[GEODE_CAST_TEXT_CAP];
    geode_cast_server_text(handle, which, out, GEODE_CAST_TEXT_CAP);
    return geode::jni::utf8String(env, out);
}

JNIEXPORT void JNICALL
Java_dev_geode_engine_bridge_GeodeNative_castServerState(JNIEnv* env, jobject, jlong handle, jlongArray out) {
    if (!out || env->GetArrayLength(out) < GEODE_CAST_STATE) return;
    int64_t values[GEODE_CAST_STATE];
    geode_cast_server_state(handle, values);
    env->SetLongArrayRegion(out, 0, GEODE_CAST_STATE, reinterpret_cast<const jlong*>(values));
}

}  // extern "C"
