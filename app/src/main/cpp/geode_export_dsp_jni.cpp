#include <jni.h>

#include <cstdint>
#include <cstring>
#include <vector>

#include "api/geode_api.h"

namespace {

geode_resampler* resamplerOf(jlong handle) { return reinterpret_cast<geode_resampler*>(handle); }
geode_loudness* loudnessOf(jlong handle) { return reinterpret_cast<geode_loudness*>(handle); }
geode_file_feed* feedOf(jlong handle) { return reinterpret_cast<geode_file_feed*>(handle); }

// The bytes [offset, offset + byteCount) of a direct ByteBuffer (a MediaCodec output buffer), or null when
// the buffer is not direct or the range does not fit inside it.
const unsigned char* directRange(JNIEnv* env, jobject buffer, jint offset, jint byteCount) {
    if (!buffer || offset < 0 || byteCount < 0) return nullptr;
    auto* base = static_cast<const unsigned char*>(env->GetDirectBufferAddress(buffer));
    const jlong capacity = env->GetDirectBufferCapacity(buffer);
    if (!base || capacity < 0 || static_cast<jlong>(offset) + byteCount > capacity) return nullptr;
    return base + offset;
}

// Layout of the double[] loudnessFinish fills; GeodeNative.loudnessFinish documents the same order.
constexpr jsize kReportDoubles = 5;

}  // namespace

extern "C" {

JNIEXPORT jlong JNICALL
Java_dev_geode_engine_bridge_GeodeNative_resamplerCreate(JNIEnv*, jobject, jint inRate, jint outRate, jint channels) {
    return reinterpret_cast<jlong>(geode_resampler_create(inRate, outRate, channels));
}

JNIEXPORT void JNICALL
Java_dev_geode_engine_bridge_GeodeNative_resamplerDestroy(JNIEnv*, jobject, jlong handle) {
    geode_resampler_destroy(resamplerOf(handle));
}

JNIEXPORT jshortArray JNICALL
Java_dev_geode_engine_bridge_GeodeNative_resamplerProcess(JNIEnv* env, jobject, jlong handle, jshortArray input,
                                                         jint frames) {
    auto* r = resamplerOf(handle);
    // The resampler's own channel count sizes everything; 64-bit maths so frames * channels cannot wrap.
    const int64_t channels = geode_resampler_channels(r);
    if (channels <= 0 || !input || frames <= 0 ||
        static_cast<int64_t>(env->GetArrayLength(input)) < static_cast<int64_t>(frames) * channels) {
        return env->NewShortArray(0);
    }
    std::vector<int16_t> in(static_cast<size_t>(frames) * static_cast<size_t>(channels));
    env->GetShortArrayRegion(input, 0, static_cast<jsize>(in.size()), reinterpret_cast<jshort*>(in.data()));
    const size_t capacity = geode_resampler_max_out_frames(r, static_cast<size_t>(frames));
    std::vector<int16_t> out(capacity * static_cast<size_t>(channels));
    const int64_t written = geode_resampler_process(r, in.data(), static_cast<size_t>(frames), out.data(), capacity);
    const auto samples = static_cast<jsize>(written > 0 ? written * channels : 0);
    jshortArray result = env->NewShortArray(samples);
    if (result && samples > 0) env->SetShortArrayRegion(result, 0, samples, reinterpret_cast<const jshort*>(out.data()));
    return result;
}

JNIEXPORT jboolean JNICALL
Java_dev_geode_engine_bridge_GeodeNative_loudnessSupports(JNIEnv*, jobject, jint channels) {
    return geode_loudness_supports(channels) ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jlong JNICALL
Java_dev_geode_engine_bridge_GeodeNative_loudnessCreate(JNIEnv*, jobject, jint sampleRate, jint channels) {
    return reinterpret_cast<jlong>(geode_loudness_create(sampleRate, channels));
}

JNIEXPORT void JNICALL
Java_dev_geode_engine_bridge_GeodeNative_loudnessDestroy(JNIEnv*, jobject, jlong handle) {
    geode_loudness_destroy(loudnessOf(handle));
}

// A decoder output buffer straight into the meter: MediaCodec hands back direct ByteBuffers of native-order
// s16 or float PCM (MediaCodec reference, "Raw audio buffers"), so the samples are read where they lie,
// with no Java array and no copy (NDK "JNI tips": share raw data through a direct buffer). [offset, size)
// is BufferInfo's span, from index 0. False, feeding nothing, when the buffer is not direct or the span
// does not fit; true otherwise, an empty span included.
JNIEXPORT jboolean JNICALL
Java_dev_geode_engine_bridge_GeodeNative_loudnessFeedDirect(JNIEnv* env, jobject, jlong handle, jobject buffer,
                                                            jint offset, jint size, jboolean isFloat) {
    if (!buffer || offset < 0 || size < 0) return JNI_FALSE;
    auto* base = static_cast<const uint8_t*>(env->GetDirectBufferAddress(buffer));
    const jlong capacity = env->GetDirectBufferCapacity(buffer);
    if (!base || capacity < 0 || static_cast<jlong>(offset) + static_cast<jlong>(size) > capacity) return JNI_FALSE;
    const uint8_t* bytes = base + offset;
    const size_t width = isFloat ? sizeof(float) : sizeof(int16_t);
    const size_t samples = static_cast<size_t>(size) / width;
    if (samples == 0) return JNI_TRUE;
    // Codec buffers are aligned in practice; a misaligned span is copied once rather than read misaligned.
    std::vector<uint8_t> aligned;
    if (reinterpret_cast<uintptr_t>(bytes) % width != 0) {
        aligned.resize(samples * width + width);
        uint8_t* start = aligned.data();
        while (reinterpret_cast<uintptr_t>(start) % width != 0) ++start;
        std::memcpy(start, bytes, samples * width);
        bytes = start;
    }
    if (isFloat) {
        geode_loudness_feed(loudnessOf(handle), reinterpret_cast<const float*>(bytes), samples);
    } else {
        geode_loudness_feed_s16(loudnessOf(handle), reinterpret_cast<const int16_t*>(bytes), samples);
    }
    return JNI_TRUE;
}

JNIEXPORT jboolean JNICALL
Java_dev_geode_engine_bridge_GeodeNative_loudnessHasBlock(JNIEnv*, jobject, jlong handle) {
    return geode_loudness_has_block(loudnessOf(handle)) ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jboolean JNICALL
Java_dev_geode_engine_bridge_GeodeNative_loudnessFinish(JNIEnv* env, jobject, jlong handle, jdoubleArray out) {
    geode_loudness_report report{};
    geode_loudness_finish(loudnessOf(handle), &report);
    if (out && env->GetArrayLength(out) >= kReportDoubles) {
        const jdouble values[kReportDoubles] = {report.integrated_lufs, report.true_peak_dbtp, report.sample_peak_dbfs,
                                                static_cast<jdouble>(report.measured_ms),
                                                static_cast<jdouble>(report.gated_block_count)};
        env->SetDoubleArrayRegion(out, 0, kReportDoubles, values);
    }
    return report.has_integrated ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jboolean JNICALL
Java_dev_geode_engine_bridge_GeodeNative_loudnessFeedPcm(JNIEnv* env, jobject, jlong handle, jobject buffer, jint offset,
                                                         jint byteCount, jint encoding) {
    const unsigned char* bytes = directRange(env, buffer, offset, byteCount);
    if (!bytes) return JNI_FALSE;
    return geode_loudness_feed_pcm(loudnessOf(handle), bytes, static_cast<size_t>(byteCount), encoding) ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jlong JNICALL
Java_dev_geode_engine_bridge_GeodeNative_fileFeedCreate(JNIEnv*, jobject, jlong startUs) {
    return reinterpret_cast<jlong>(geode_file_feed_create(startUs));
}

JNIEXPORT void JNICALL
Java_dev_geode_engine_bridge_GeodeNative_fileFeedDestroy(JNIEnv*, jobject, jlong handle) {
    geode_file_feed_destroy(feedOf(handle));
}

JNIEXPORT jlong JNICALL
Java_dev_geode_engine_bridge_GeodeNative_fileFeedWrite(JNIEnv* env, jobject, jlong handle, jobject buffer, jint offset,
                                                       jint byteCount, jint encoding, jint sampleRate, jint channels,
                                                       jlong ptsUs) {
    const unsigned char* bytes = directRange(env, buffer, offset, byteCount);
    if (!bytes) return -1;
    return geode_file_feed_write(feedOf(handle), bytes, static_cast<size_t>(byteCount), encoding, sampleRate, channels, ptsUs);
}

// AIFF: AiffPcm already hands 16-bit samples in a short[] (no timestamps; the feed counts from the top).
JNIEXPORT jlong JNICALL
Java_dev_geode_engine_bridge_GeodeNative_fileFeedWriteShorts(JNIEnv* env, jobject, jlong handle, jshortArray samples,
                                                             jint count, jint sampleRate, jint channels) {
    if (!samples || count <= 0) return 0;
    const jint n = count < env->GetArrayLength(samples) ? count : env->GetArrayLength(samples);
    void* data = env->GetPrimitiveArrayCritical(samples, nullptr);
    if (!data) return -1;
    const long long kept = geode_file_feed_write(feedOf(handle), data, static_cast<size_t>(n) * sizeof(jshort),
                                                 2 /* ENCODING_PCM_16BIT */, sampleRate, channels, -1);
    env->ReleasePrimitiveArrayCritical(samples, data, JNI_ABORT);
    return kept;
}

JNIEXPORT jboolean JNICALL
Java_dev_geode_engine_bridge_GeodeNative_fileFeedNeeds(JNIEnv*, jobject, jlong handle, jlong frameIndex, jint fps) {
    return geode_file_feed_needs(feedOf(handle), frameIndex, fps) ? JNI_TRUE : JNI_FALSE;
}

}
