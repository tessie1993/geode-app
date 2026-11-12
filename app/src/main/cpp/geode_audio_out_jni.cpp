#include <jni.h>

#include "api/geode_api.h"

// The playback output (core/audio/output): values in, values out, nothing else. A handle is valid from
// audioOutOpen until audioOutRelease. The write reads Media3's direct buffer where it lies; the audio
// callback that plays it never enters the JVM.

namespace {
geode_audio_out* outOf(jlong handle) { return reinterpret_cast<geode_audio_out*>(handle); }
}  // namespace

extern "C" {

JNIEXPORT void JNICALL Java_dev_geode_engine_bridge_GeodeNative_audioOutNoteFailed(JNIEnv*, jobject, jlong nowMs) {
    geode_audio_out_note_failed(nowMs);
}

JNIEXPORT jboolean JNICALL Java_dev_geode_engine_bridge_GeodeNative_audioOutMayOpen(JNIEnv*, jobject, jlong nowMs) {
    return geode_audio_out_may_open(nowMs) ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jboolean JNICALL Java_dev_geode_engine_bridge_GeodeNative_audioOutSupports(JNIEnv*, jobject, jint encoding,
                                                                                     jint channels,
                                                                                     jint sampleRateHz) {
    return geode_audio_out_supports(encoding, channels, sampleRateHz) ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jlong JNICALL Java_dev_geode_engine_bridge_GeodeNative_audioOutOpen(JNIEnv*, jobject, jint sampleRateHz,
                                                                              jint channels, jint encoding,
                                                                              jint bufferBytes, jint sessionId) {
    return reinterpret_cast<jlong>(geode_audio_out_open(sampleRateHz, channels, encoding, bufferBytes, sessionId));
}

JNIEXPORT void JNICALL Java_dev_geode_engine_bridge_GeodeNative_audioOutRelease(JNIEnv*, jobject, jlong handle) {
    geode_audio_out_release(outOf(handle));
}

JNIEXPORT void JNICALL Java_dev_geode_engine_bridge_GeodeNative_audioOutPlay(JNIEnv*, jobject, jlong handle) {
    geode_audio_out_play(outOf(handle));
}

JNIEXPORT void JNICALL Java_dev_geode_engine_bridge_GeodeNative_audioOutPause(JNIEnv*, jobject, jlong handle) {
    geode_audio_out_pause(outOf(handle));
}

JNIEXPORT void JNICALL Java_dev_geode_engine_bridge_GeodeNative_audioOutFlush(JNIEnv*, jobject, jlong handle) {
    geode_audio_out_flush(outOf(handle));
}

JNIEXPORT void JNICALL Java_dev_geode_engine_bridge_GeodeNative_audioOutStop(JNIEnv*, jobject, jlong handle) {
    geode_audio_out_stop(outOf(handle));
}

// Media3's output buffer: read where it lies, no copy and no Java array. -1 for a heap buffer or a
// range outside it, as for a failed stream.
JNIEXPORT jint JNICALL Java_dev_geode_engine_bridge_GeodeNative_audioOutWrite(JNIEnv* env, jobject, jlong handle,
                                                                              jobject buffer, jint position,
                                                                              jint bytes) {
    if (!buffer || position < 0 || bytes < 0) return -1;
    const auto* address = static_cast<const unsigned char*>(env->GetDirectBufferAddress(buffer));
    if (!address || env->GetDirectBufferCapacity(buffer) < static_cast<jlong>(position) + bytes) return -1;
    return static_cast<jint>(geode_audio_out_write(outOf(handle), address + position, static_cast<size_t>(bytes)));
}

JNIEXPORT jlong JNICALL Java_dev_geode_engine_bridge_GeodeNative_audioOutPositionUs(JNIEnv*, jobject, jlong handle) {
    return geode_audio_out_position_us(outOf(handle));
}

JNIEXPORT void JNICALL Java_dev_geode_engine_bridge_GeodeNative_audioOutSetVolume(JNIEnv*, jobject, jlong handle,
                                                                                  jfloat volume) {
    geode_audio_out_set_volume(outOf(handle), volume);
}

JNIEXPORT void JNICALL Java_dev_geode_engine_bridge_GeodeNative_audioOutSetDevice(JNIEnv*, jobject, jlong handle,
                                                                                  jint deviceId) {
    geode_audio_out_set_device(outOf(handle), deviceId);
}

JNIEXPORT jint JNICALL Java_dev_geode_engine_bridge_GeodeNative_audioOutEvents(JNIEnv*, jobject, jlong handle) {
    return geode_audio_out_events(outOf(handle));
}

JNIEXPORT jboolean JNICALL Java_dev_geode_engine_bridge_GeodeNative_audioOutStalled(JNIEnv*, jobject, jlong handle) {
    return geode_audio_out_stalled(outOf(handle)) ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jint JNICALL Java_dev_geode_engine_bridge_GeodeNative_audioOutSessionId(JNIEnv*, jobject, jlong handle) {
    return geode_audio_out_session_id(outOf(handle));
}

JNIEXPORT jlong JNICALL Java_dev_geode_engine_bridge_GeodeNative_audioOutBufferFrames(JNIEnv*, jobject,
                                                                                      jlong handle) {
    return geode_audio_out_buffer_frames(outOf(handle));
}

}  // extern "C"
