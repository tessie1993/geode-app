#include <jni.h>

#include <algorithm>
#include <cstdint>

#include "api/geode_api.h"
#include "jni_util.hpp"

extern "C" {

JNIEXPORT jlong JNICALL
Java_dev_geode_engine_bridge_GeodeNative_silenceHeardAt(JNIEnv* env, jobject, jfloatArray left, jfloatArray right,
                                                        jint frames, jlong nowMs, jlong heardAtMs) {
    geode::jni::FloatElements l(env, left);
    geode::jni::FloatElements r(env, right);
    if (!l.get() || !r.get()) return heardAtMs;
    const int n = std::min({static_cast<int>(frames), static_cast<int>(l.length()), static_cast<int>(r.length())});
    const float* channels[2] = {l.get(), r.get()};
    return geode_silence_heard_at(channels, 2, n, nowMs, heardAtMs);
}

JNIEXPORT jboolean JNICALL
Java_dev_geode_engine_bridge_GeodeNative_silenceResting(JNIEnv*, jobject, jlong nowMs, jlong heardAtMs) {
    return geode_silence_resting(nowMs, heardAtMs) ? JNI_TRUE : JNI_FALSE;
}

// Native ingest ring (core/audio/ingest, #480). Only marshalling: the handle is the geode_ingest pointer,
// and a write passes the direct buffer's bytes from `offset` for `length` bytes. The audio thread must not
// copy or allocate here, so the buffer must be direct; its capacity bounds the range so native can never
// read past it, whatever the caller passes.

static geode_ingest* ingestOf(jlong handle) {
    return reinterpret_cast<geode_ingest*>(static_cast<intptr_t>(handle));
}

JNIEXPORT jlong JNICALL
Java_dev_geode_engine_bridge_GeodeNative_ingestCreate(JNIEnv*, jobject, jint capacityFrames, jint maxChannels) {
    return static_cast<jlong>(reinterpret_cast<intptr_t>(geode_ingest_create(capacityFrames, maxChannels)));
}

JNIEXPORT void JNICALL
Java_dev_geode_engine_bridge_GeodeNative_ingestDestroy(JNIEnv*, jobject, jlong handle) {
    geode_ingest_destroy(ingestOf(handle));
}

JNIEXPORT void JNICALL
Java_dev_geode_engine_bridge_GeodeNative_ingestBeginEpoch(JNIEnv*, jobject, jlong handle, jint sampleRate, jint channels,
                                                          jint channelMask) {
    geode_ingest_begin_epoch(ingestOf(handle), sampleRate, channels, channelMask);
}

JNIEXPORT void JNICALL
Java_dev_geode_engine_bridge_GeodeNative_ingestSetFormat(JNIEnv*, jobject, jlong handle, jint sampleRate, jint channels,
                                                         jint channelMask) {
    geode_ingest_set_format(ingestOf(handle), sampleRate, channels, channelMask);
}

JNIEXPORT jint JNICALL
Java_dev_geode_engine_bridge_GeodeNative_ingestWrite(JNIEnv* env, jobject, jlong handle, jobject buffer, jint offset,
                                                     jint length, jint encoding) {
    if (!buffer || offset < 0 || length <= 0) return 0;
    auto* base = static_cast<const unsigned char*>(env->GetDirectBufferAddress(buffer));
    const jlong capacity = env->GetDirectBufferCapacity(buffer);
    if (!base || capacity <= 0 || static_cast<jlong>(offset) + static_cast<jlong>(length) > capacity) return 0;
    return static_cast<jint>(
        geode_ingest_write_bytes(ingestOf(handle), base + offset, static_cast<size_t>(length), encoding));
}

JNIEXPORT jlong JNICALL
Java_dev_geode_engine_bridge_GeodeNative_ingestWritten(JNIEnv*, jobject, jlong handle) {
    return geode_ingest_written(ingestOf(handle));
}

JNIEXPORT jboolean JNICALL
Java_dev_geode_engine_bridge_GeodeNative_ingestResting(JNIEnv*, jobject, jlong handle, jlong nowMs) {
    return geode_ingest_resting(ingestOf(handle), nowMs) ? JNI_TRUE : JNI_FALSE;
}

}  // extern "C"
