#include <jni.h>

#include <cstdint>

#include "api/geode_api.h"

// Capture takes (geode_take_*, ADR 0008): values only. The record tap's buffer crosses as a direct
// ByteBuffer, so the playback thread copies nothing on the way in.
extern "C" {

JNIEXPORT jboolean JNICALL
Java_dev_geode_engine_bridge_GeodeNative_takeBegin(JNIEnv*, jobject, jint fd, jfloat displayFps) {
    return geode_take_begin(fd, displayFps) != 0 ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jlong JNICALL
Java_dev_geode_engine_bridge_GeodeNative_takeEnd(JNIEnv*, jobject) {
    return static_cast<jlong>(geode_take_end());
}

JNIEXPORT jboolean JNICALL
Java_dev_geode_engine_bridge_GeodeNative_takeActive(JNIEnv*, jobject) {
    return geode_take_active() != 0 ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT void JNICALL
Java_dev_geode_engine_bridge_GeodeNative_takeWriteAudio(JNIEnv* env, jobject, jobject buffer, jint offset, jint bytes,
                                                         jint channels, jint sampleRate) {
    if (!buffer || offset < 0 || bytes <= 0 || channels <= 0) return;
    auto* base = static_cast<uint8_t*>(env->GetDirectBufferAddress(buffer));
    const jlong capacity = env->GetDirectBufferCapacity(buffer);
    if (!base || capacity < static_cast<jlong>(offset) + bytes) return;
    const int frames = bytes / (2 * channels);
    geode_take_write_audio(reinterpret_cast<const int16_t*>(base + offset), frames, channels, sampleRate);
}

}
