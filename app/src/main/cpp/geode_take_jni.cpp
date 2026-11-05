#include <jni.h>

#include <cstdint>
#include <vector>

#include "api/geode_api.h"

// Capture takes (geode_take_*, ADR 0008): values only. The record tap's buffer crosses as a direct
// ByteBuffer, so the playback thread copies nothing on the way in.
extern "C" {

JNIEXPORT jboolean JNICALL
Java_dev_geode_engine_bridge_GeodeNative_takeBeginLimited(JNIEnv* env, jobject, jint fd, jfloat displayFps, jlong capMs,
                                                           jintArray tile, jint tileW, jint tileH, jint tileShortSide) {
    std::vector<uint32_t> pixels;
    const jsize n = tile ? env->GetArrayLength(tile) : 0;
    if (n > 0 && n >= tileW * tileH) {
        pixels.resize(static_cast<size_t>(n));
        env->GetIntArrayRegion(tile, 0, n, reinterpret_cast<jint*>(pixels.data()));
    }
    const uint32_t* argb = pixels.empty() ? nullptr : pixels.data();
    return geode_take_begin_limited(fd, displayFps, capMs, argb, tileW, tileH, tileShortSide) != 0 ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jboolean JNICALL
Java_dev_geode_engine_bridge_GeodeNative_takeReachedCap(JNIEnv*, jobject) {
    return geode_take_reached_cap() != 0 ? JNI_TRUE : JNI_FALSE;
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
