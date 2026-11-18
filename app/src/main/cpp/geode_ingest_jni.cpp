#include <jni.h>

#include <cstdint>
#include <mutex>
#include <vector>

#include "api/geode_api.h"
#include "api/geode_capture_java_api.h"

// The PCM ingest ring (core/audio/ingest/PcmRing): values in, values out, nothing else. The player's and
// the mic's handles are never freed; the output mix's is freed by its owner.

namespace {
geode_pcm_ring* ringOf(jlong handle) { return reinterpret_cast<geode_pcm_ring*>(handle); }

// ByteBuffer's methods for a heap buffer, looked up once (a boot class, never unloaded).
struct HeapBufferIds {
    jmethodID duplicate = nullptr;
    jmethodID position = nullptr;
    jmethodID get = nullptr;
};

const HeapBufferIds& heapBufferIds(JNIEnv* env) {
    static const HeapBufferIds ids = [env] {
        HeapBufferIds k;
        jclass c = env->FindClass("java/nio/ByteBuffer");
        if (c) {
            k.duplicate = env->GetMethodID(c, "duplicate", "()Ljava/nio/ByteBuffer;");
            if (k.duplicate) k.position = env->GetMethodID(c, "position", "(I)Ljava/nio/Buffer;");
            if (k.position) k.get = env->GetMethodID(c, "get", "([BII)Ljava/nio/ByteBuffer;");
            env->DeleteLocalRef(c);
        }
        env->ExceptionClear();
        return k;
    }();
    return ids;
}

// A heap buffer, read-only or not, has no address: its bytes are copied by the JVM's bulk get (on a
// duplicate, so its position stays the shell's) into one array native keeps, grown only when too small,
// then written from that array pinned. The tap is the one writer, so the lock is never contended.
std::mutex gHeapLock;
jbyteArray gHeapBytes = nullptr;
jsize gHeapLength = 0;

int writeHeap(JNIEnv* env, geode_pcm_ring* ring, jobject buffer, jint position, jint bytes) {
    const HeapBufferIds& ids = heapBufferIds(env);
    if (!ids.get) return 0;
    std::lock_guard<std::mutex> lock(gHeapLock);
    if (gHeapLength < bytes) {
        jbyteArray grown = env->NewByteArray(bytes);
        if (!grown) {
            env->ExceptionClear();
            return 0;
        }
        if (gHeapBytes) env->DeleteGlobalRef(gHeapBytes);
        gHeapBytes = static_cast<jbyteArray>(env->NewGlobalRef(grown));
        env->DeleteLocalRef(grown);
        gHeapLength = gHeapBytes ? bytes : 0;
        if (!gHeapBytes) return 0;
    }
    jobject view = env->CallObjectMethod(buffer, ids.duplicate);
    if (!env->ExceptionCheck() && view) env->DeleteLocalRef(env->CallObjectMethod(view, ids.position, position));
    if (!env->ExceptionCheck() && view) env->DeleteLocalRef(env->CallObjectMethod(view, ids.get, gHeapBytes, 0, bytes));
    if (view) env->DeleteLocalRef(view);
    if (env->ExceptionCheck() || !view) {
        env->ExceptionClear();  // a position or length past the buffer: nothing to write
        return 0;
    }
    void* data = env->GetPrimitiveArrayCritical(gHeapBytes, nullptr);
    if (!data) return 0;
    const int written = geode_pcm_ring_tap_write(ring, data, static_cast<size_t>(bytes));
    env->ReleasePrimitiveArrayCritical(gHeapBytes, data, JNI_ABORT);
    return written;
}
}  // namespace

extern "C" {

JNIEXPORT jlong JNICALL Java_dev_geode_engine_bridge_IngestNative_pcmRingShared(JNIEnv*, jobject, jint kind) {
    return reinterpret_cast<jlong>(geode_pcm_ring_shared(kind));
}

JNIEXPORT jlong JNICALL Java_dev_geode_engine_bridge_IngestNative_pcmRingCreateKind(JNIEnv*, jobject, jint kind) {
    return reinterpret_cast<jlong>(geode_pcm_ring_create_kind(kind));
}

JNIEXPORT void JNICALL Java_dev_geode_engine_bridge_IngestNative_pcmRingDestroy(JNIEnv*, jobject, jlong handle) {
    geode_pcm_ring_destroy(ringOf(handle));
}

// The tap's buffer: a direct one read where it lies, a heap one through one bulk copy (writeHeap).
JNIEXPORT jint JNICALL Java_dev_geode_engine_bridge_IngestNative_pcmTapWrite(JNIEnv* env, jobject, jlong handle,
                                                                            jobject buffer, jint position,
                                                                            jint bytes) {
    if (!buffer || position < 0 || bytes <= 0) return 0;
    const auto* address = static_cast<const unsigned char*>(env->GetDirectBufferAddress(buffer));
    if (address) {
        if (env->GetDirectBufferCapacity(buffer) < static_cast<jlong>(position) + bytes) return 0;
        return geode_pcm_ring_tap_write(ringOf(handle), address + position, static_cast<size_t>(bytes));
    }
    return writeHeap(env, ringOf(handle), buffer, position, bytes);
}

JNIEXPORT void JNICALL Java_dev_geode_engine_bridge_IngestNative_pcmTapFlush(JNIEnv*, jobject, jlong handle,
                                                                            jint sampleRate, jint channels,
                                                                            jint encoding) {
    geode_pcm_ring_tap_flush(ringOf(handle), sampleRate, channels, encoding);
}

JNIEXPORT void JNICALL Java_dev_geode_engine_bridge_IngestNative_pcmTapSetOwner(JNIEnv*, jobject, jlong handle,
                                                                               jboolean owns) {
    geode_pcm_ring_tap_set_owner(ringOf(handle), owns ? 1 : 0);
}

JNIEXPORT jint JNICALL Java_dev_geode_engine_bridge_IngestNative_pcmRingMaxWriteFrames(JNIEnv*, jobject, jlong handle) {
    return geode_pcm_ring_max_write_frames(ringOf(handle));
}

JNIEXPORT void JNICALL Java_dev_geode_engine_bridge_IngestNative_pcmRingBeginEpoch(JNIEnv*, jobject, jlong handle) {
    geode_pcm_ring_begin_epoch(ringOf(handle));
}

JNIEXPORT jlong JNICALL Java_dev_geode_engine_bridge_IngestNative_pcmRingWritten(JNIEnv*, jobject, jlong handle) {
    return geode_pcm_ring_written(ringOf(handle));
}

JNIEXPORT jlong JNICALL Java_dev_geode_engine_bridge_IngestNative_pcmRingHead(JNIEnv*, jobject, jlong handle) {
    return geode_pcm_ring_head(ringOf(handle));
}

JNIEXPORT void JNICALL Java_dev_geode_engine_bridge_IngestNative_pcmRingSetPlayout(JNIEnv*, jobject, jlong handle,
                                                                                  jlong playout) {
    geode_pcm_ring_set_playout(ringOf(handle), reinterpret_cast<const geode_playout*>(playout));
}

JNIEXPORT jlong JNICALL Java_dev_geode_engine_bridge_IngestNative_pcmRingHeardAt(JNIEnv*, jobject, jlong handle,
                                                                                jint frames, jlong nowMs,
                                                                                jlong heardAtMs) {
    return geode_pcm_ring_heard_at(ringOf(handle), frames, nowMs, heardAtMs);
}

// The header scan (core/audio/ingest/AiffHeaderScan) for the player's extractor, which holds the file.
JNIEXPORT jlong JNICALL Java_dev_geode_engine_bridge_IngestNative_aiffScanCreate(JNIEnv*, jobject) {
    return reinterpret_cast<jlong>(geode_aiff_scan_create());
}

JNIEXPORT void JNICALL Java_dev_geode_engine_bridge_IngestNative_aiffScanDestroy(JNIEnv*, jobject, jlong handle) {
    geode_aiff_scan_destroy(reinterpret_cast<geode_aiff_scan*>(handle));
}

JNIEXPORT jlong JNICALL Java_dev_geode_engine_bridge_IngestNative_aiffScanSkip(JNIEnv*, jobject, jlong handle) {
    return geode_aiff_scan_skip(reinterpret_cast<const geode_aiff_scan*>(handle));
}

JNIEXPORT jint JNICALL Java_dev_geode_engine_bridge_IngestNative_aiffScanRead(JNIEnv*, jobject, jlong handle) {
    return geode_aiff_scan_read(reinterpret_cast<const geode_aiff_scan*>(handle));
}

// The first n bytes of `bytes`, copied out: the scan never asks for more than GEODE_AIFF_SCAN_MAX_READ.
JNIEXPORT jint JNICALL Java_dev_geode_engine_bridge_IngestNative_aiffScanStep(JNIEnv* env, jobject, jlong handle,
                                                                             jbyteArray bytes, jint n) {
    if (!bytes || n < 0 || n > GEODE_AIFF_SCAN_MAX_READ || env->GetArrayLength(bytes) < n) return GEODE_AIFF_SCAN_INVALID;
    jbyte copy[GEODE_AIFF_SCAN_MAX_READ] = {};
    env->GetByteArrayRegion(bytes, 0, n, copy);
    return geode_aiff_scan_step(reinterpret_cast<geode_aiff_scan*>(handle), reinterpret_cast<const uint8_t*>(copy), n);
}

// out: GEODE_AIFF_HEADER_COUNT values, in the GEODE_AIFF_HEADER_* order.
JNIEXPORT jboolean JNICALL Java_dev_geode_engine_bridge_IngestNative_aiffScanHeader(JNIEnv* env, jobject, jlong handle,
                                                                                   jlongArray out) {
    if (!out || env->GetArrayLength(out) < GEODE_AIFF_HEADER_COUNT) return JNI_FALSE;
    int64_t values[GEODE_AIFF_HEADER_COUNT] = {};
    const int ok = geode_aiff_scan_header(reinterpret_cast<const geode_aiff_scan*>(handle), values,
                                          GEODE_AIFF_HEADER_COUNT);
    env->SetLongArrayRegion(out, 0, GEODE_AIFF_HEADER_COUNT, reinterpret_cast<const jlong*>(values));
    return ok ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jlong JNICALL Java_dev_geode_engine_bridge_IngestNative_aiffTimeUs(JNIEnv*, jobject, jlong bytes,
                                                                            jint bytesPerFrame, jint sampleRate) {
    return geode_aiff_time_us(bytes, bytesPerFrame, sampleRate);
}

JNIEXPORT jlong JNICALL Java_dev_geode_engine_bridge_IngestNative_aiffSeekOffset(JNIEnv*, jobject, jlong timeUs,
                                                                                jint bytesPerFrame, jint sampleRate,
                                                                                jlong dataBytes) {
    return geode_aiff_seek_offset(timeUs, bytesPerFrame, sampleRate, dataBytes);
}

JNIEXPORT jlong JNICALL Java_dev_geode_engine_bridge_IngestNative_aiffGapBytes(JNIEnv*, jobject, jlong position,
                                                                              jlong dataStart) {
    return geode_aiff_gap_bytes(position, dataStart);
}

JNIEXPORT jint JNICALL Java_dev_geode_engine_bridge_IngestNative_aiffReadBytes(JNIEnv*, jobject, jlong position,
                                                                              jlong dataStart, jlong dataBytes) {
    return geode_aiff_read_bytes(position, dataStart, dataBytes);
}

JNIEXPORT jint JNICALL Java_dev_geode_engine_bridge_IngestNative_aiffWholeBytes(JNIEnv*, jobject, jint pending,
                                                                               jint bytesPerFrame) {
    return geode_aiff_whole_bytes(pending, bytesPerFrame);
}

JNIEXPORT jlong JNICALL Java_dev_geode_engine_bridge_IngestNative_aiffPendingTimeUs(JNIEnv*, jobject, jlong position,
                                                                                   jlong dataStart, jint pending,
                                                                                   jint bytesPerFrame,
                                                                                   jint sampleRate) {
    return geode_aiff_pending_time_us(position, dataStart, pending, bytesPerFrame, sampleRate);
}

JNIEXPORT jboolean JNICALL Java_dev_geode_engine_bridge_IngestNative_micStart(JNIEnv*, jobject, jint deviceRateHz,
                                                                             jboolean unprocessed) {
    return geode_mic_start(deviceRateHz, unprocessed ? 1 : 0) ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT void JNICALL Java_dev_geode_engine_bridge_IngestNative_micStop(JNIEnv*, jobject) { geode_mic_stop(); }

JNIEXPORT jboolean JNICALL Java_dev_geode_engine_bridge_IngestNative_micActive(JNIEnv*, jobject) {
    return geode_mic_active() ? JNI_TRUE : JNI_FALSE;
}

// The captures Android has only as Java objects (geode_capture_java_api.h): built, read and released natively.
JNIEXPORT jlong JNICALL Java_dev_geode_engine_bridge_IngestNative_javaCapturePlayback(JNIEnv* env, jobject,
                                                                                     jobject projection, jlong ring) {
    return reinterpret_cast<jlong>(geode_java_capture_playback(env, projection, ringOf(ring)));
}

JNIEXPORT jlong JNICALL Java_dev_geode_engine_bridge_IngestNative_javaCaptureOutputMix(JNIEnv* env, jobject,
                                                                                      jlong ring) {
    return reinterpret_cast<jlong>(geode_java_capture_output_mix(env, ringOf(ring)));
}

JNIEXPORT void JNICALL Java_dev_geode_engine_bridge_IngestNative_javaCaptureStop(JNIEnv*, jobject, jlong handle) {
    geode_java_capture_stop(reinterpret_cast<geode_java_capture*>(handle));
}

JNIEXPORT jboolean JNICALL Java_dev_geode_engine_bridge_IngestNative_javaCaptureActive(JNIEnv*, jobject,
                                                                                     jlong handle) {
    return geode_java_capture_active(reinterpret_cast<const geode_java_capture*>(handle)) ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jboolean JNICALL Java_dev_geode_engine_bridge_IngestNative_javaCaptureQuiet(JNIEnv*, jobject, jlong handle) {
    return geode_java_capture_quiet(reinterpret_cast<const geode_java_capture*>(handle)) ? JNI_TRUE : JNI_FALSE;
}

}  // extern "C"
