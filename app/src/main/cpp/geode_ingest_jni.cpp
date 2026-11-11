#include <jni.h>

#include "api/geode_api.h"

// The PCM ingest ring (core/audio/ingest/PcmRing): values in, values out, nothing else. A ring handle is
// valid for as long as its Kotlin holder (SampleRing) is reachable, which frees it; a cursor handle until
// its holder closes it. The array writes pin the array (critical) for one lock-free copy: no JNI call,
// no lock and no allocation happens inside.

namespace {
geode_pcm_ring* ringOf(jlong handle) { return reinterpret_cast<geode_pcm_ring*>(handle); }
geode_pcm_cursor* cursorOf(jlong handle) { return reinterpret_cast<geode_pcm_cursor*>(handle); }
}  // namespace

extern "C" {

JNIEXPORT jlong JNICALL Java_dev_geode_engine_bridge_GeodeNative_pcmRingCreate(JNIEnv*, jobject, jint capacityFrames,
                                                                               jint channels, jint maxWriteFrames) {
    return reinterpret_cast<jlong>(geode_pcm_ring_create(capacityFrames, channels, maxWriteFrames));
}

JNIEXPORT void JNICALL Java_dev_geode_engine_bridge_GeodeNative_pcmRingDestroy(JNIEnv*, jobject, jlong handle) {
    geode_pcm_ring_destroy(ringOf(handle));
}

// The tap's buffer: read where it lies, no copy and no Java array.
JNIEXPORT jint JNICALL Java_dev_geode_engine_bridge_GeodeNative_pcmRingWriteDirect(JNIEnv* env, jobject, jlong handle,
                                                                                   jobject buffer, jint position,
                                                                                   jint bytes, jint encoding,
                                                                                   jint channels, jint sampleRate) {
    if (!buffer || position < 0 || bytes <= 0) return 0;
    const auto* address = static_cast<const unsigned char*>(env->GetDirectBufferAddress(buffer));
    if (!address || env->GetDirectBufferCapacity(buffer) < static_cast<jlong>(position) + bytes) return 0;
    return geode_pcm_ring_write_bytes(ringOf(handle), address + position, static_cast<size_t>(bytes), encoding,
                                      channels, sampleRate, nullptr);
}

JNIEXPORT jfloat JNICALL Java_dev_geode_engine_bridge_GeodeNative_pcmRingWriteFloats(JNIEnv* env, jobject,
                                                                                     jlong handle, jfloatArray samples,
                                                                                     jint frames, jint channels,
                                                                                     jint sampleRate) {
    if (!samples || frames <= 0 || channels <= 0) return 0.0f;
    const jint held = env->GetArrayLength(samples) / channels;
    float peak = 0.0f;
    void* data = env->GetPrimitiveArrayCritical(samples, nullptr);
    if (!data) return 0.0f;
    geode_pcm_ring_write_f32(ringOf(handle), static_cast<const float*>(data), frames < held ? frames : held, channels,
                             sampleRate, &peak);
    env->ReleasePrimitiveArrayCritical(samples, data, JNI_ABORT);
    return peak;
}

JNIEXPORT jfloat JNICALL Java_dev_geode_engine_bridge_GeodeNative_pcmRingWriteShorts(JNIEnv* env, jobject,
                                                                                     jlong handle, jshortArray samples,
                                                                                     jint frames, jint channels,
                                                                                     jint sampleRate) {
    if (!samples || frames <= 0 || channels <= 0) return 0.0f;
    const jint held = env->GetArrayLength(samples) / channels;
    float peak = 0.0f;
    void* data = env->GetPrimitiveArrayCritical(samples, nullptr);
    if (!data) return 0.0f;
    geode_pcm_ring_write_s16(ringOf(handle), static_cast<const int16_t*>(data), frames < held ? frames : held,
                             channels, sampleRate, &peak);
    env->ReleasePrimitiveArrayCritical(samples, data, JNI_ABORT);
    return peak;
}

JNIEXPORT jint JNICALL Java_dev_geode_engine_bridge_GeodeNative_pcmRingWriteBytes(JNIEnv* env, jobject, jlong handle,
                                                                                  jbyteArray samples, jint count,
                                                                                  jint encoding, jint channels,
                                                                                  jint sampleRate) {
    if (!samples || count <= 0) return 0;
    const jint held = env->GetArrayLength(samples);
    void* data = env->GetPrimitiveArrayCritical(samples, nullptr);
    if (!data) return 0;
    const int frames = geode_pcm_ring_write_bytes(ringOf(handle), data, static_cast<size_t>(count < held ? count : held),
                                                  encoding, channels, sampleRate, nullptr);
    env->ReleasePrimitiveArrayCritical(samples, data, JNI_ABORT);
    return frames;
}

JNIEXPORT void JNICALL Java_dev_geode_engine_bridge_GeodeNative_pcmRingBeginEpoch(JNIEnv*, jobject, jlong handle) {
    geode_pcm_ring_begin_epoch(ringOf(handle));
}

JNIEXPORT jlong JNICALL Java_dev_geode_engine_bridge_GeodeNative_pcmRingWritten(JNIEnv*, jobject, jlong handle) {
    return geode_pcm_ring_written(ringOf(handle));
}

JNIEXPORT jlong JNICALL Java_dev_geode_engine_bridge_GeodeNative_pcmRingHead(JNIEnv*, jobject, jlong handle) {
    return geode_pcm_ring_head(ringOf(handle));
}

JNIEXPORT void JNICALL Java_dev_geode_engine_bridge_GeodeNative_pcmRingSetPlayout(JNIEnv*, jobject, jlong handle,
                                                                                  jlong playout) {
    geode_pcm_ring_set_playout(ringOf(handle), reinterpret_cast<const geode_playout*>(playout));
}

JNIEXPORT jlong JNICALL Java_dev_geode_engine_bridge_GeodeNative_pcmRingHeardAt(JNIEnv*, jobject, jlong handle,
                                                                                jint frames, jlong nowMs,
                                                                                jlong heardAtMs) {
    return geode_pcm_ring_heard_at(ringOf(handle), frames, nowMs, heardAtMs);
}

JNIEXPORT jlong JNICALL Java_dev_geode_engine_bridge_GeodeNative_pcmCursorCreate(JNIEnv*, jobject, jlong ring,
                                                                                 jint maxFrames) {
    return reinterpret_cast<jlong>(geode_pcm_cursor_create(ringOf(ring), maxFrames));
}

JNIEXPORT void JNICALL Java_dev_geode_engine_bridge_GeodeNative_pcmCursorDestroy(JNIEnv*, jobject, jlong handle) {
    geode_pcm_cursor_destroy(cursorOf(handle));
}

JNIEXPORT jint JNICALL Java_dev_geode_engine_bridge_GeodeNative_pcmCursorMaxFrames(JNIEnv*, jobject, jlong handle) {
    return geode_pcm_cursor_max_frames(cursorOf(handle));
}

// info, when given: [0] 1 on a restart, [1] the rate the frames were written at.
JNIEXPORT jint JNICALL Java_dev_geode_engine_bridge_GeodeNative_pcmCursorTakeS16(JNIEnv* env, jobject, jlong handle,
                                                                                 jshortArray out, jintArray info) {
    geode_pcm_cursor* cursor = cursorOf(handle);
    const jint channels = geode_pcm_cursor_channels(cursor);
    if (!out || channels <= 0) return 0;
    const jint capacity = env->GetArrayLength(out) / channels;
    int restarted = 0;
    int rate = 0;
    void* data = env->GetPrimitiveArrayCritical(out, nullptr);
    if (!data) return 0;
    const int frames = geode_pcm_cursor_take_s16(cursor, static_cast<int16_t*>(data), capacity, &restarted, &rate);
    env->ReleasePrimitiveArrayCritical(out, data, 0);
    if (info && env->GetArrayLength(info) >= 2) {
        const jint values[2] = {restarted, rate};
        env->SetIntArrayRegion(info, 0, 2, values);
    }
    return frames < 0 ? 0 : frames;
}

}  // extern "C"
