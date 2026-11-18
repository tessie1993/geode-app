#include <jni.h>

#include <string>
#include <vector>

#include "api/geode_api.h"
#include "jni_util.hpp"

namespace {

geode_dsp* dspOf(jlong handle) { return reinterpret_cast<geode_dsp*>(handle); }

// ByteBuffer.put(ByteBuffer) and position(int), looked up once (a boot class, never unloaded).
struct BulkPutIds {
    jmethodID put = nullptr;
    jmethodID position = nullptr;
};

const BulkPutIds& bulkPutIds(JNIEnv* env) {
    static const BulkPutIds ids = [env] {
        BulkPutIds k;
        jclass c = env->FindClass("java/nio/ByteBuffer");
        if (c) {
            k.put = env->GetMethodID(c, "put", "(Ljava/nio/ByteBuffer;)Ljava/nio/ByteBuffer;");
            if (k.put) k.position = env->GetMethodID(c, "position", "(I)Ljava/nio/Buffer;");
            env->DeleteLocalRef(c);
        }
        env->ExceptionClear();
        return k;
    }();
    return ids;
}

}  // namespace

extern "C" {

JNIEXPORT jlong JNICALL
Java_dev_geode_engine_bridge_GeodeNative_dspCreate(JNIEnv*, jobject) {
    return reinterpret_cast<jlong>(geode_dsp_create());
}

JNIEXPORT jboolean JNICALL
Java_dev_geode_engine_bridge_GeodeNative_dspConfigure(JNIEnv*, jobject, jlong handle, jint encoding, jint sampleRate,
                                                      jint channels) {
    return geode_dsp_configure_input(dspOf(handle), encoding, sampleRate, channels) != 0 ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jboolean JNICALL
Java_dev_geode_engine_bridge_GeodeNative_dspRuns(JNIEnv*, jobject, jint encoding, jint channels) {
    return geode_dsp_runs(encoding, channels) != 0 ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT void JNICALL
Java_dev_geode_engine_bridge_GeodeNative_dspFlush(JNIEnv*, jobject, jlong handle) {
    geode_dsp_flush(dspOf(handle));
}

JNIEXPORT void JNICALL
Java_dev_geode_engine_bridge_GeodeNative_dspDestroy(JNIEnv*, jobject, jlong handle) {
    geode_dsp_destroy(dspOf(handle));
}

JNIEXPORT jint JNICALL
Java_dev_geode_engine_bridge_GeodeNative_dspEqPresetCount(JNIEnv*, jobject) {
    return geode_dsp_eq_preset_count();
}

JNIEXPORT jint JNICALL
Java_dev_geode_engine_bridge_GeodeNative_dspEqPresetLevels(JNIEnv* env, jobject, jint preset, jintArray out) {
    int32_t levels[GEODE_DSP_BANDS] = {};
    const int n = geode_dsp_eq_preset_levels(preset, levels, GEODE_DSP_BANDS);
    if (out == nullptr || n <= 0 || env->GetArrayLength(out) < n) return 0;
    env->SetIntArrayRegion(out, 0, n, static_cast<const jint*>(levels));
    return n;
}

JNIEXPORT jint JNICALL
Java_dev_geode_engine_bridge_GeodeNative_dspBandCount(JNIEnv*, jobject) {
    return geode_dsp_band_count();
}

JNIEXPORT jint JNICALL
Java_dev_geode_engine_bridge_GeodeNative_dspFxMin(JNIEnv*, jobject, jint control) {
    return geode_dsp_fx_min(control);
}

JNIEXPORT jint JNICALL
Java_dev_geode_engine_bridge_GeodeNative_dspFxMax(JNIEnv*, jobject, jint control) {
    return geode_dsp_fx_max(control);
}

JNIEXPORT jint JNICALL
Java_dev_geode_engine_bridge_GeodeNative_dspFxClamp(JNIEnv*, jobject, jint control, jint value) {
    return geode_dsp_fx_clamp(control, value);
}

JNIEXPORT jint JNICALL
Java_dev_geode_engine_bridge_GeodeNative_dspRestoreBandsText(JNIEnv* env, jobject, jint preset, jstring csv, jintArray out) {
    if (out == nullptr || env->GetArrayLength(out) < GEODE_DSP_BANDS) return 0;
    const geode::jni::Utf8Chars text(env, csv);
    int32_t levels[GEODE_DSP_BANDS] = {};
    const int n = geode_dsp_restore_bands_text(preset, text.get(), levels, GEODE_DSP_BANDS);
    env->SetIntArrayRegion(out, 0, n, static_cast<const jint*>(levels));
    return n;
}

JNIEXPORT jstring JNICALL
Java_dev_geode_engine_bridge_GeodeNative_dspEncodeBands(JNIEnv* env, jobject, jintArray levels) {
    const jsize count = levels ? env->GetArrayLength(levels) : 0;
    std::vector<jint> values(static_cast<size_t>(count));
    if (count > 0) env->GetIntArrayRegion(levels, 0, count, values.data());
    std::string text(static_cast<size_t>(geode_dsp_encode_bands(values.data(), count, nullptr, 0)) + 1, '\0');
    geode_dsp_encode_bands(values.data(), count, text.data(), static_cast<int>(text.size()));
    return geode::jni::utf8String(env, text.c_str());
}

JNIEXPORT jstring JNICALL
Java_dev_geode_engine_bridge_GeodeNative_dspBandLabel(JNIEnv* env, jobject, jint band) {
    char text[32] = {};
    geode_dsp_band_label(band, text, static_cast<int>(sizeof text));
    return geode::jni::utf8String(env, text);
}

JNIEXPORT jstring JNICALL
Java_dev_geode_engine_bridge_GeodeNative_dspDbLabel(JNIEnv* env, jobject, jint millibels) {
    char text[32] = {};
    geode_dsp_db_label(millibels, text, static_cast<int>(sizeof text));
    return geode::jni::utf8String(env, text);
}

JNIEXPORT void JNICALL
Java_dev_geode_engine_bridge_GeodeNative_dspSetEnabled(JNIEnv*, jobject, jlong handle, jboolean enabled) {
    geode_dsp_set_enabled(dspOf(handle), enabled == JNI_TRUE);
}

JNIEXPORT void JNICALL
Java_dev_geode_engine_bridge_GeodeNative_dspSetBand(JNIEnv*, jobject, jlong handle, jint band, jint millibels) {
    geode_dsp_set_band(dspOf(handle), band, millibels);
}

JNIEXPORT void JNICALL
Java_dev_geode_engine_bridge_GeodeNative_dspSetBassBoost(JNIEnv*, jobject, jlong handle, jint permille) {
    geode_dsp_set_bass_boost(dspOf(handle), permille);
}

JNIEXPORT void JNICALL
Java_dev_geode_engine_bridge_GeodeNative_dspSetLoudnessMb(JNIEnv*, jobject, jlong handle, jint millibels) {
    geode_dsp_set_loudness_mb(dspOf(handle), millibels);
}

JNIEXPORT void JNICALL
Java_dev_geode_engine_bridge_GeodeNative_dspRampAudible(JNIEnv*, jobject, jlong handle, jboolean audible, jint ms) {
    geode_dsp_ramp_audible(dspOf(handle), audible == JNI_TRUE, ms);
}

JNIEXPORT jlong JNICALL
Java_dev_geode_engine_bridge_GeodeNative_dspSleepFade(JNIEnv*, jobject, jlong handle, jlong remainingMs) {
    return static_cast<jlong>(geode_dsp_sleep_fade(dspOf(handle), static_cast<int64_t>(remainingMs)));
}

JNIEXPORT void JNICALL
Java_dev_geode_engine_bridge_GeodeNative_dspSetCrossfade(JNIEnv*, jobject, jlong handle, jint ms) {
    geode_dsp_set_crossfade(dspOf(handle), ms);
}

JNIEXPORT void JNICALL
Java_dev_geode_engine_bridge_GeodeNative_dspSetUpNext(JNIEnv*, jobject, jlong handle, jboolean hasNext, jint repeatMode) {
    geode_dsp_set_up_next(dspOf(handle), hasNext == JNI_TRUE, repeatMode);
}

JNIEXPORT void JNICALL
Java_dev_geode_engine_bridge_GeodeNative_dspSetSpeed(JNIEnv*, jobject, jlong handle, jfloat speed) {
    geode_dsp_set_speed(dspOf(handle), speed);
}

JNIEXPORT void JNICALL
Java_dev_geode_engine_bridge_GeodeNative_dspSetReplayGain(JNIEnv*, jobject, jlong handle, jint mode, jfloat preampDb,
                                                          jboolean clipGuard) {
    geode_dsp_set_replaygain(dspOf(handle), mode, preampDb, clipGuard == JNI_TRUE);
}

JNIEXPORT void JNICALL
Java_dev_geode_engine_bridge_GeodeNative_dspSetStreamTags(JNIEnv* env, jobject, jlong handle, jstring stream, jint mask,
                                                          jfloat trackGainDb, jfloat trackPeak, jfloat albumGainDb,
                                                          jfloat albumPeak) {
    if (!stream) return;
    const geode::jni::Utf8Chars name(env, stream);
    if (!name.get()) return;
    geode_dsp_set_stream_tags(dspOf(handle), name.get(), mask, trackGainDb, trackPeak, albumGainDb, albumPeak);
}

// Playback thread, at the processor's flush (not per block), so the string copy here is not on the hot path.
JNIEXPORT void JNICALL
Java_dev_geode_engine_bridge_GeodeNative_dspBeginStream(JNIEnv* env, jobject, jlong handle, jstring stream, jlong positionUs,
                                                        jlong durationUs) {
    const geode::jni::Utf8Chars name(env, stream);
    geode_dsp_begin_stream(dspOf(handle), name.get(), positionUs, durationUs);
}

JNIEXPORT void JNICALL
Java_dev_geode_engine_bridge_GeodeNative_dspEndStream(JNIEnv*, jobject, jlong handle) {
    geode_dsp_end_stream(dspOf(handle));
}

JNIEXPORT void JNICALL
Java_dev_geode_engine_bridge_GeodeNative_dspSetCrossfeed(JNIEnv*, jobject, jlong handle, jboolean enabled) {
    geode_dsp_set_crossfeed(dspOf(handle), enabled == JNI_TRUE);
}

JNIEXPORT void JNICALL
Java_dev_geode_engine_bridge_GeodeNative_dspSetLimiter(JNIEnv*, jobject, jlong handle, jboolean enabled) {
    geode_dsp_set_limiter(dspOf(handle), enabled == JNI_TRUE);
}

JNIEXPORT jfloat JNICALL
Java_dev_geode_engine_bridge_GeodeNative_playbackPitchRatio(JNIEnv*, jobject, jfloat semitones) {
    return geode_playback_pitch_ratio(semitones);
}

JNIEXPORT jfloatArray JNICALL
Java_dev_geode_engine_bridge_GeodeNative_playbackRange(JNIEnv* env, jobject, jint control) {
    float range[4];
    if (!geode_playback_range(control, range)) return nullptr;
    jfloatArray out = env->NewFloatArray(4);
    if (out) env->SetFloatArrayRegion(out, 0, 4, range);
    return out;
}

JNIEXPORT jfloat JNICALL
Java_dev_geode_engine_bridge_GeodeNative_playbackClamp(JNIEnv*, jobject, jint control, jfloat value) {
    return geode_playback_clamp(control, value);
}

JNIEXPORT jint JNICALL
Java_dev_geode_engine_bridge_GeodeNative_playbackCrossfadeMs(JNIEnv*, jobject, jfloat seconds) {
    return geode_playback_crossfade_ms(seconds);
}

JNIEXPORT jfloat JNICALL
Java_dev_geode_engine_bridge_GeodeNative_playbackSnap(JNIEnv*, jobject, jfloat value, jfloat step) {
    return geode_playback_snap(value, step);
}

JNIEXPORT jint JNICALL
Java_dev_geode_engine_bridge_GeodeNative_bookmarkVerdict(JNIEnv*, jobject, jlong positionMs, jlong durationMs) {
    return geode_bookmark_verdict(positionMs, durationMs);
}

JNIEXPORT jboolean JNICALL
Java_dev_geode_engine_bridge_GeodeNative_bookmarkResumesFrom(JNIEnv*, jobject, jlong currentPositionMs) {
    return geode_bookmark_resumes_from(currentPositionMs) != 0 ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jint JNICALL
Java_dev_geode_engine_bridge_GeodeNative_playbackErrorAction(JNIEnv*, jobject, jint consecutiveFailures, jboolean hasNext) {
    return geode_playback_error_action(consecutiveFailures, hasNext == JNI_TRUE);
}

// The A-B loop's next points as a new long[2] (start, end; -1 not set).
JNIEXPORT jlongArray JNICALL
Java_dev_geode_engine_bridge_GeodeNative_abLoopCycle(JNIEnv* env, jobject, jlong startMs, jlong endMs, jlong atMs) {
    int64_t next[2] = {-1, -1};
    geode_ab_loop_cycle(startMs, endMs, atMs, next);
    const jlong out[2] = {static_cast<jlong>(next[0]), static_cast<jlong>(next[1])};
    jlongArray result = env->NewLongArray(2);
    if (result) env->SetLongArrayRegion(result, 0, 2, out);
    return result;
}

JNIEXPORT jlong JNICALL
Java_dev_geode_engine_bridge_GeodeNative_abLoopSeek(JNIEnv*, jobject, jlong startMs, jlong endMs, jlong positionMs) {
    return geode_ab_loop_seek(startMs, endMs, positionMs);
}

JNIEXPORT void JNICALL
Java_dev_geode_engine_bridge_GeodeNative_dspSetBitPerfect(JNIEnv*, jobject, jlong handle, jboolean on) {
    geode_dsp_set_bit_perfect(dspOf(handle), on == JNI_TRUE);
}

// Media3's direct buffers where they lie; the output's capacity goes along so native can never walk past it
// (issue #356). An input with no address (a heap block; every processor before this one hands out direct
// buffers) is copied by the JVM's bulk put, then run in place as any other.
JNIEXPORT jint JNICALL
Java_dev_geode_engine_bridge_GeodeNative_dspProcessInto(JNIEnv* env, jobject, jlong handle, jobject input, jint position,
                                                        jint bytes, jobject output, jint encoding, jint channels) {
    if (!input || !output || position < 0 || bytes <= 0) return 0;
    auto* out = env->GetDirectBufferAddress(output);
    const jlong capacity = out ? env->GetDirectBufferCapacity(output) : 0;
    if (capacity <= 0) return 0;
    const auto* in = static_cast<const unsigned char*>(env->GetDirectBufferAddress(input));
    if (in) {
        if (env->GetDirectBufferCapacity(input) < static_cast<jlong>(position) + bytes) return 0;
        return geode_dsp_process_into(dspOf(handle), in + position, bytes, out, static_cast<size_t>(capacity), encoding,
                                      channels);
    }
    const BulkPutIds& ids = bulkPutIds(env);
    if (!ids.position || bytes > capacity) return 0;
    env->DeleteLocalRef(env->CallObjectMethod(output, ids.put, input));
    if (!env->ExceptionCheck()) env->DeleteLocalRef(env->CallObjectMethod(output, ids.position, 0));
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        return 0;
    }
    return geode_dsp_process_bytes(dspOf(handle), out, static_cast<size_t>(capacity), bytes, encoding, channels);
}

}
