// Show on a computer over WebRTC (core/cast/WebRtcLink through geode_webrtc_*): marshalling only.
#include <jni.h>

#include <cstdint>

#include "api/geode_api.h"
#include "jni_util.hpp"

extern "C" {

JNIEXPORT jlong JNICALL
Java_dev_geode_engine_bridge_WebrtcNative_webrtcOpen(JNIEnv* env, jobject, jstring boundDotted, jint bound, jint prefix) {
    geode::jni::Utf8Chars b(env, boundDotted);
    return static_cast<jlong>(geode_webrtc_open(b.get(), static_cast<uint32_t>(bound), prefix));
}

JNIEXPORT void JNICALL
Java_dev_geode_engine_bridge_WebrtcNative_webrtcClose(JNIEnv*, jobject, jlong handle) {
    geode_webrtc_close(handle);
}

}  // extern "C"
