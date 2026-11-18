#include <jni.h>

#include <cstdint>
#include <cstring>
#include <algorithm>
#include <vector>

#include "api/geode_api.h"

extern "C" {

// Encoder fallback ladder (core/output/EncoderLadder): marshalling only. ceilings is one entry per candidate
// codec; the int[] returned is [codecIndex, fps, bitRate] triples in the order to try, the layout
// ExportDspNative.encoderAttempts documents.
JNIEXPORT jintArray JNICALL
Java_dev_geode_engine_bridge_ExportDspNative_encoderAttempts(JNIEnv* env, jobject, jint requestedFps, jint bitRate,
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
