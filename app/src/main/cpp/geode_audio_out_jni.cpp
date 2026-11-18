#include <jni.h>

#include "api/geode_api.h"

// The playback output (core/audio/output): values in, values out, nothing else. A handle is valid from
// audioOutOpen until audioOutRelease. The write reads Media3's direct buffer where it lies; the audio
// callback that plays it never enters the JVM.

namespace {
geode_audio_out* outOf(jlong handle) { return reinterpret_cast<geode_audio_out*>(handle); }
}  // namespace

extern "C" {

JNIEXPORT void JNICALL Java_dev_geode_engine_bridge_AudioOutNative_audioOutNoteFailed(JNIEnv*, jobject, jlong nowMs) {
    geode_audio_out_note_failed(nowMs);
}

JNIEXPORT jboolean JNICALL Java_dev_geode_engine_bridge_AudioOutNative_audioOutWanted(JNIEnv*, jobject, jboolean chosen,
                                                                                   jboolean bitPerfect,
                                                                                   jboolean bitPerfectSupported) {
    return geode_audio_out_wanted(chosen == JNI_TRUE, bitPerfect == JNI_TRUE, bitPerfectSupported == JNI_TRUE)
               ? JNI_TRUE
               : JNI_FALSE;
}

JNIEXPORT jboolean JNICALL Java_dev_geode_engine_bridge_AudioOutNative_audioOutTakes(
    JNIEnv*, jobject, jlong nowMs, jboolean offload, jboolean tunneling, jboolean offloadGapless,
    jboolean playbackParameters, jint virtualDeviceId, jint usage, jint channelMask, jint encoding, jint sampleRateHz) {
    return geode_audio_out_takes(nowMs, offload == JNI_TRUE, tunneling == JNI_TRUE, offloadGapless == JNI_TRUE,
                                 playbackParameters == JNI_TRUE, virtualDeviceId, usage, channelMask, encoding,
                                 sampleRateHz)
               ? JNI_TRUE
               : JNI_FALSE;
}

JNIEXPORT jlong JNICALL Java_dev_geode_engine_bridge_AudioOutNative_audioOutOpen(JNIEnv*, jobject, jint sampleRateHz,
                                                                              jint channelMask, jint encoding,
                                                                              jint bufferBytes, jint sessionId) {
    return reinterpret_cast<jlong>(geode_audio_out_open(sampleRateHz, channelMask, encoding, bufferBytes, sessionId));
}

JNIEXPORT void JNICALL Java_dev_geode_engine_bridge_AudioOutNative_audioOutRelease(JNIEnv*, jobject, jlong handle) {
    geode_audio_out_release(outOf(handle));
}

JNIEXPORT void JNICALL Java_dev_geode_engine_bridge_AudioOutNative_audioOutPlay(JNIEnv*, jobject, jlong handle) {
    geode_audio_out_play(outOf(handle));
}

JNIEXPORT void JNICALL Java_dev_geode_engine_bridge_AudioOutNative_audioOutPause(JNIEnv*, jobject, jlong handle) {
    geode_audio_out_pause(outOf(handle));
}

JNIEXPORT void JNICALL Java_dev_geode_engine_bridge_AudioOutNative_audioOutFlush(JNIEnv*, jobject, jlong handle) {
    geode_audio_out_flush(outOf(handle));
}

JNIEXPORT void JNICALL Java_dev_geode_engine_bridge_AudioOutNative_audioOutStop(JNIEnv*, jobject, jlong handle) {
    geode_audio_out_stop(outOf(handle));
}

// Media3's output buffer: read where it lies, no copy and no Java array. -1 for a heap buffer or a
// range outside it, as for a failed stream.
JNIEXPORT jint JNICALL Java_dev_geode_engine_bridge_AudioOutNative_audioOutWrite(JNIEnv* env, jobject, jlong handle,
                                                                              jobject buffer, jint position,
                                                                              jint bytes) {
    if (!buffer || position < 0 || bytes < 0) return -1;
    const auto* address = static_cast<const unsigned char*>(env->GetDirectBufferAddress(buffer));
    if (!address || env->GetDirectBufferCapacity(buffer) < static_cast<jlong>(position) + bytes) return -1;
    return static_cast<jint>(geode_audio_out_write(outOf(handle), address + position, static_cast<size_t>(bytes)));
}

JNIEXPORT jlong JNICALL Java_dev_geode_engine_bridge_AudioOutNative_audioOutPositionUs(JNIEnv*, jobject, jlong handle) {
    return geode_audio_out_position_us(outOf(handle));
}

JNIEXPORT void JNICALL Java_dev_geode_engine_bridge_AudioOutNative_audioOutSetVolume(JNIEnv*, jobject, jlong handle,
                                                                                  jfloat volume) {
    geode_audio_out_set_volume(outOf(handle), volume);
}

JNIEXPORT void JNICALL Java_dev_geode_engine_bridge_AudioOutNative_audioOutSetDevice(JNIEnv*, jobject, jlong handle,
                                                                                  jint deviceId) {
    geode_audio_out_set_device(outOf(handle), deviceId);
}

JNIEXPORT jint JNICALL Java_dev_geode_engine_bridge_AudioOutNative_audioOutEvents(JNIEnv*, jobject, jlong handle) {
    return geode_audio_out_events(outOf(handle));
}

JNIEXPORT jboolean JNICALL Java_dev_geode_engine_bridge_AudioOutNative_audioOutStalled(JNIEnv*, jobject, jlong handle) {
    return geode_audio_out_stalled(outOf(handle)) ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jint JNICALL Java_dev_geode_engine_bridge_AudioOutNative_audioOutSessionId(JNIEnv*, jobject, jlong handle) {
    return geode_audio_out_session_id(outOf(handle));
}

JNIEXPORT jlong JNICALL Java_dev_geode_engine_bridge_AudioOutNative_audioOutBufferFrames(JNIEnv*, jobject,
                                                                                      jlong handle) {
    return geode_audio_out_buffer_frames(outOf(handle));
}

}  // extern "C"
