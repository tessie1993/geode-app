#include <jni.h>

#include <cstdint>
#include <vector>

#include "api/geode_api.h"

// The PCM ingest ring (core/audio/ingest/PcmRing): values in, values out, nothing else. A ring handle is
// valid for as long as its Kotlin holder (SampleRing) is reachable, which frees it. The array writes pin
// the array (critical) for one lock-free copy: no JNI call, no lock and no allocation happens inside.

namespace {
geode_pcm_ring* ringOf(jlong handle) { return reinterpret_cast<geode_pcm_ring*>(handle); }
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

JNIEXPORT void JNICALL Java_dev_geode_engine_bridge_GeodeNative_pcmRingSetLayer(JNIEnv*, jobject, jlong handle,
                                                                                jlong layer) {
    geode_pcm_ring_set_layer(ringOf(handle), ringOf(layer));
}

JNIEXPORT jboolean JNICALL Java_dev_geode_engine_bridge_GeodeNative_pcmRingSetSnapshots(JNIEnv*, jobject, jlong handle,
                                                                                        jboolean on) {
    return geode_pcm_ring_set_snapshots(ringOf(handle), on ? 1 : 0) ? JNI_TRUE : JNI_FALSE;
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

// AIFF (core/audio/ingest/AiffReader): the descriptor is handed over and closed natively.
JNIEXPORT jlong JNICALL Java_dev_geode_engine_bridge_GeodeNative_aiffOpenFd(JNIEnv*, jobject, jint fd,
                                                                            jlong startOffset, jlong length) {
    return reinterpret_cast<jlong>(geode_aiff_open_fd(fd, startOffset, length));
}

JNIEXPORT void JNICALL Java_dev_geode_engine_bridge_GeodeNative_aiffClose(JNIEnv*, jobject, jlong handle) {
    geode_aiff_close(reinterpret_cast<geode_aiff*>(handle));
}

// out: [0] channels, [1] sample rate, [2] the COMM frame count.
JNIEXPORT void JNICALL Java_dev_geode_engine_bridge_GeodeNative_aiffInfo(JNIEnv* env, jobject, jlong handle,
                                                                         jlongArray out) {
    if (!out || env->GetArrayLength(out) < 3) return;
    const auto* a = reinterpret_cast<const geode_aiff*>(handle);
    const jlong values[3] = {geode_aiff_channels(a), geode_aiff_sample_rate(a), geode_aiff_total_frames(a)};
    env->SetLongArrayRegion(out, 0, 3, values);
}

JNIEXPORT jfloat JNICALL Java_dev_geode_engine_bridge_GeodeNative_aiffProgress(JNIEnv*, jobject, jlong handle) {
    return geode_aiff_progress(reinterpret_cast<const geode_aiff*>(handle));
}

// File I/O happens in the read, so the array is filled from a native buffer rather than pinned across it.
JNIEXPORT jint JNICALL Java_dev_geode_engine_bridge_GeodeNative_aiffReadS16(JNIEnv* env, jobject, jlong handle,
                                                                            jshortArray out) {
    if (!out) return -1;
    const jsize capacity = env->GetArrayLength(out);
    std::vector<int16_t> samples(static_cast<size_t>(capacity));
    const int n = geode_aiff_read_s16(reinterpret_cast<geode_aiff*>(handle), samples.data(), capacity);
    if (n > 0) env->SetShortArrayRegion(out, 0, n, samples.data());
    return n;
}

// The header scan (core/audio/ingest/AiffHeaderScan) for the player's extractor, which holds the file.
JNIEXPORT jlong JNICALL Java_dev_geode_engine_bridge_GeodeNative_aiffScanCreate(JNIEnv*, jobject) {
    return reinterpret_cast<jlong>(geode_aiff_scan_create());
}

JNIEXPORT void JNICALL Java_dev_geode_engine_bridge_GeodeNative_aiffScanDestroy(JNIEnv*, jobject, jlong handle) {
    geode_aiff_scan_destroy(reinterpret_cast<geode_aiff_scan*>(handle));
}

JNIEXPORT jlong JNICALL Java_dev_geode_engine_bridge_GeodeNative_aiffScanSkip(JNIEnv*, jobject, jlong handle) {
    return geode_aiff_scan_skip(reinterpret_cast<const geode_aiff_scan*>(handle));
}

JNIEXPORT jint JNICALL Java_dev_geode_engine_bridge_GeodeNative_aiffScanRead(JNIEnv*, jobject, jlong handle) {
    return geode_aiff_scan_read(reinterpret_cast<const geode_aiff_scan*>(handle));
}

// The first n bytes of `bytes`, copied out: the scan never asks for more than GEODE_AIFF_SCAN_MAX_READ.
JNIEXPORT jint JNICALL Java_dev_geode_engine_bridge_GeodeNative_aiffScanStep(JNIEnv* env, jobject, jlong handle,
                                                                             jbyteArray bytes, jint n) {
    if (!bytes || n < 0 || n > GEODE_AIFF_SCAN_MAX_READ || env->GetArrayLength(bytes) < n) return GEODE_AIFF_SCAN_INVALID;
    jbyte copy[GEODE_AIFF_SCAN_MAX_READ] = {};
    env->GetByteArrayRegion(bytes, 0, n, copy);
    return geode_aiff_scan_step(reinterpret_cast<geode_aiff_scan*>(handle), reinterpret_cast<const uint8_t*>(copy), n);
}

// out: GEODE_AIFF_HEADER_COUNT values, in the GEODE_AIFF_HEADER_* order.
JNIEXPORT jboolean JNICALL Java_dev_geode_engine_bridge_GeodeNative_aiffScanHeader(JNIEnv* env, jobject, jlong handle,
                                                                                   jlongArray out) {
    if (!out || env->GetArrayLength(out) < GEODE_AIFF_HEADER_COUNT) return JNI_FALSE;
    int64_t values[GEODE_AIFF_HEADER_COUNT] = {};
    const int ok = geode_aiff_scan_header(reinterpret_cast<const geode_aiff_scan*>(handle), values,
                                          GEODE_AIFF_HEADER_COUNT);
    env->SetLongArrayRegion(out, 0, GEODE_AIFF_HEADER_COUNT, reinterpret_cast<const jlong*>(values));
    return ok ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jlong JNICALL Java_dev_geode_engine_bridge_GeodeNative_aiffTimeUs(JNIEnv*, jobject, jlong bytes,
                                                                            jint bytesPerFrame, jint sampleRate) {
    return geode_aiff_time_us(bytes, bytesPerFrame, sampleRate);
}

JNIEXPORT jlong JNICALL Java_dev_geode_engine_bridge_GeodeNative_aiffSeekOffset(JNIEnv*, jobject, jlong timeUs,
                                                                                jint bytesPerFrame, jint sampleRate,
                                                                                jlong dataBytes) {
    return geode_aiff_seek_offset(timeUs, bytesPerFrame, sampleRate, dataBytes);
}

// A capture's format plan (core/audio/ingest/CapturePlan): GEODE_CAPTURE_FORMAT_INTS ints a format.
JNIEXPORT jintArray JNICALL Java_dev_geode_engine_bridge_GeodeNative_captureFormats(JNIEnv* env, jobject,
                                                                                   jint deviceRateHz, jboolean stereo) {
    constexpr int kMaxRows = 16;
    int rows[kMaxRows * GEODE_CAPTURE_FORMAT_INTS] = {};
    const int count = geode_capture_plan(deviceRateHz, stereo ? 1 : 0, rows, kMaxRows);
    const int n = (count < kMaxRows ? count : kMaxRows) * GEODE_CAPTURE_FORMAT_INTS;
    jint values[kMaxRows * GEODE_CAPTURE_FORMAT_INTS];
    for (int i = 0; i < n; ++i) values[i] = rows[i];
    jintArray out = env->NewIntArray(n);
    if (out) env->SetIntArrayRegion(out, 0, n, values);
    return out;
}

JNIEXPORT jint JNICALL Java_dev_geode_engine_bridge_GeodeNative_captureBufferBytes(JNIEnv*, jobject,
                                                                                   jint minBufferBytes, jint channels) {
    return geode_capture_buffer_bytes(minBufferBytes, channels);
}

JNIEXPORT jint JNICALL Java_dev_geode_engine_bridge_GeodeNative_captureReadFrames(JNIEnv*, jobject) {
    return geode_capture_read_frames();
}

JNIEXPORT jboolean JNICALL Java_dev_geode_engine_bridge_GeodeNative_micStart(JNIEnv*, jobject, jlong ring,
                                                                             jint deviceRateHz, jboolean unprocessed) {
    return geode_mic_start(ringOf(ring), deviceRateHz, unprocessed ? 1 : 0) ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT void JNICALL Java_dev_geode_engine_bridge_GeodeNative_micStop(JNIEnv*, jobject) { geode_mic_stop(); }

JNIEXPORT jboolean JNICALL Java_dev_geode_engine_bridge_GeodeNative_micActive(JNIEnv*, jobject) {
    return geode_mic_active() ? JNI_TRUE : JNI_FALSE;
}

}  // extern "C"
