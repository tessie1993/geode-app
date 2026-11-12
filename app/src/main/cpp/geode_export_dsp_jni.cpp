#include <jni.h>

#include <cstdint>
#include <cstring>
#include <algorithm>
#include <vector>

#include "api/geode_api.h"

namespace {

geode_resampler* resamplerOf(jlong handle) { return reinterpret_cast<geode_resampler*>(handle); }

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

// Encoder fallback ladder (core/output/EncoderLadder): marshalling only. ceilings is one entry per candidate
// codec; the int[] returned is [codecIndex, fps, bitRate] triples in the order to try, the layout
// GeodeNative.encoderAttempts documents.
JNIEXPORT jintArray JNICALL
Java_dev_geode_engine_bridge_GeodeNative_encoderAttempts(JNIEnv* env, jobject, jint requestedFps, jint bitRate,
                                                        jintArray ceilings) {
    constexpr jsize kMaxCodecs = 2;   // geode_encoder_attempts reads at most this many
    constexpr int kCapacity = 4;      // and writes at most this many attempts
    jint ceil[kMaxCodecs] = {};
    const jsize codecs = ceilings ? std::min(env->GetArrayLength(ceilings), kMaxCodecs) : 0;
    if (codecs > 0) env->GetIntArrayRegion(ceilings, 0, codecs, ceil);
    int codec[kCapacity] = {};
    int fps[kCapacity] = {};
    int bps[kCapacity] = {};
    const int count = geode_encoder_attempts(requestedFps, bitRate, ceil, codecs, codec, fps, bps, kCapacity);
    jint triples[kCapacity * 3] = {};
    for (int i = 0; i < count; ++i) {
        triples[i * 3] = codec[i];
        triples[i * 3 + 1] = fps[i];
        triples[i * 3 + 2] = bps[i];
    }
    jintArray result = env->NewIntArray(count * 3);
    if (result && count > 0) env->SetIntArrayRegion(result, 0, count * 3, triples);
    return result;
}

}
