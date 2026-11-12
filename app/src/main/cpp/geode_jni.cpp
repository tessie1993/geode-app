#include <jni.h>

#include <string>
#include <vector>

#include "api/geode_api.h"

namespace {

geode_dsp* dspOf(jlong handle) { return reinterpret_cast<geode_dsp*>(handle); }

}  // namespace

extern "C" {

JNIEXPORT jlong JNICALL
Java_dev_geode_engine_bridge_GeodeNative_dspCreate(JNIEnv*, jobject) {
    return reinterpret_cast<jlong>(geode_dsp_create());
}

JNIEXPORT jboolean JNICALL
Java_dev_geode_engine_bridge_GeodeNative_dspConfigure(JNIEnv*, jobject, jlong handle, jint sampleRate, jint channels) {
    return geode_dsp_configure(dspOf(handle), sampleRate, channels) != 0 ? JNI_TRUE : JNI_FALSE;
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

JNIEXPORT jfloat JNICALL
Java_dev_geode_engine_bridge_GeodeNative_dspBandCenterHz(JNIEnv*, jobject, jint band) {
    return geode_dsp_band_center_hz(band);
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
    const char* text = csv ? env->GetStringUTFChars(csv, nullptr) : nullptr;
    int32_t levels[GEODE_DSP_BANDS] = {};
    const int n = geode_dsp_restore_bands_text(preset, text, levels, GEODE_DSP_BANDS);
    if (text) env->ReleaseStringUTFChars(csv, text);
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
    return env->NewStringUTF(text.c_str());
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
Java_dev_geode_engine_bridge_GeodeNative_dspRampTo(JNIEnv*, jobject, jlong handle, jfloat db, jint ms) {
    geode_dsp_ramp_to(dspOf(handle), db, ms);
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
Java_dev_geode_engine_bridge_GeodeNative_dspSetFollowsOn(JNIEnv*, jobject, jlong handle, jboolean followsOn) {
    geode_dsp_set_follows_on(dspOf(handle), followsOn == JNI_TRUE);
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
    const char* name = env->GetStringUTFChars(stream, nullptr);
    if (!name) return;
    geode_dsp_set_stream_tags(dspOf(handle), name, mask, trackGainDb, trackPeak, albumGainDb, albumPeak);
    env->ReleaseStringUTFChars(stream, name);
}

// Playback thread, at the processor's flush (not per block), so the string copy here is not on the hot path.
JNIEXPORT void JNICALL
Java_dev_geode_engine_bridge_GeodeNative_dspBeginStream(JNIEnv* env, jobject, jlong handle, jstring stream, jlong positionUs,
                                                        jlong durationUs) {
    const char* name = stream ? env->GetStringUTFChars(stream, nullptr) : nullptr;
    geode_dsp_begin_stream(dspOf(handle), name, positionUs, durationUs);
    if (name) env->ReleaseStringUTFChars(stream, name);
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

JNIEXPORT void JNICALL
Java_dev_geode_engine_bridge_GeodeNative_dspSetBitPerfect(JNIEnv*, jobject, jlong handle, jboolean on) {
    geode_dsp_set_bit_perfect(dspOf(handle), on == JNI_TRUE);
}

// The buffer must be direct: the audio thread must not copy or allocate here. Its capacity goes along so
// native can never walk past it (issue #356). `pcmFloat` only picks which of the two ABI calls gets it.
JNIEXPORT void JNICALL
Java_dev_geode_engine_bridge_GeodeNative_dspProcess(JNIEnv* env, jobject, jlong handle, jobject buffer, jint frames, jint channels,
                                                    jboolean pcmFloat) {
    if (!buffer) return;
    void* address = env->GetDirectBufferAddress(buffer);
    const jlong capacityBytes = env->GetDirectBufferCapacity(buffer);
    if (!address || capacityBytes <= 0 || frames <= 0 || channels <= 0) return;
    const auto bytes = static_cast<size_t>(capacityBytes);
    if (pcmFloat == JNI_TRUE) {
        geode_dsp_process(dspOf(handle), static_cast<float*>(address), bytes / sizeof(float), static_cast<size_t>(frames), channels);
    } else {
        geode_dsp_process_pcm16(dspOf(handle), static_cast<int16_t*>(address), bytes / sizeof(int16_t), static_cast<size_t>(frames), channels);
    }
}

}
