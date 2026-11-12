// JNI marshalling for a loop render (core/api/geode_loop_job_api.h). Handles and values only: the job, its
// frame loop, its timing and its files are core/output/LoopRender. The shell polls; nothing here calls back,
// so no JavaVM or global reference is kept. loopJobRun blocks its (worker) thread for the whole render.
#include <jni.h>

#include <android/asset_manager_jni.h>

#include <cstdint>

#include "api/geode_loop_job_api.h"
#include "jni_util.hpp"

namespace {

using geode::jni::FloatElements;
using geode::jni::Utf8Chars;

geode_loop_job* jobOf(jlong handle) { return reinterpret_cast<geode_loop_job*>(handle); }

}  // namespace

extern "C" {

JNIEXPORT jlong JNICALL Java_dev_geode_engine_bridge_GeodeNative_loopJobCreate(JNIEnv*, jobject) {
    return reinterpret_cast<jlong>(geode_loop_job_create());
}

JNIEXPORT void JNICALL Java_dev_geode_engine_bridge_GeodeNative_loopJobDestroy(JNIEnv*, jobject, jlong job) {
    geode_loop_job_destroy(jobOf(job));
}

JNIEXPORT void JNICALL Java_dev_geode_engine_bridge_GeodeNative_loopJobSetOutput(JNIEnv* env, jobject, jlong job,
                                                                                 jstring cacheDir, jint width,
                                                                                 jint height, jint bitRate, jlong loopMs,
                                                                                 jlong crossfadeMs, jint fps,
                                                                                 jlong longFormMs) {
    Utf8Chars dir(env, cacheDir);
    geode_loop_job_set_output(jobOf(job), dir.get(), width, height, bitRate, loopMs, crossfadeMs, fps, longFormMs);
}

JNIEXPORT void JNICALL Java_dev_geode_engine_bridge_GeodeNative_loopJobSetDrift(JNIEnv*, jobject, jlong job,
                                                                                jfloat hueTurns, jfloat warmth,
                                                                                jint stops) {
    geode_loop_job_set_drift(jobOf(job), hueTurns, warmth, stops);
}

JNIEXPORT void JNICALL Java_dev_geode_engine_bridge_GeodeNative_loopJobAddCodec(JNIEnv* env, jobject, jlong job,
                                                                                jstring mime, jint topFps) {
    Utf8Chars type(env, mime);
    geode_loop_job_add_codec(jobOf(job), type.get(), topFps);
}

JNIEXPORT void JNICALL Java_dev_geode_engine_bridge_GeodeNative_loopJobSetTrack(JNIEnv*, jobject, jlong job, jint fd,
                                                                                jlong offset, jlong length) {
    geode_loop_job_set_track(jobOf(job), fd, offset, length);
}

JNIEXPORT void JNICALL Java_dev_geode_engine_bridge_GeodeNative_loopJobSetScene(JNIEnv* env, jobject, jlong job,
                                                                                jobject assetManager,
                                                                                jstring milkTextureDir, jstring sceneId,
                                                                                jstring milkPath, jboolean entitled) {
    Utf8Chars textures(env, milkTextureDir);
    Utf8Chars scene(env, sceneId);
    Utf8Chars milk(env, milkPath);
    geode_loop_job_set_scene(jobOf(job), assetManager ? AAssetManager_fromJava(env, assetManager) : nullptr,
                             textures.get(), scene.get(), milk.get(), entitled == JNI_TRUE ? 1 : 0);
}

JNIEXPORT void JNICALL Java_dev_geode_engine_bridge_GeodeNative_loopJobSetParams(JNIEnv* env, jobject, jlong job,
                                                                                 jfloatArray values) {
    FloatElements data(env, values);
    geode_loop_job_set_params(jobOf(job), data.get(), data.length());
}

JNIEXPORT void JNICALL Java_dev_geode_engine_bridge_GeodeNative_loopJobSetLfos(JNIEnv* env, jobject, jlong job,
                                                                               jstring lfos) {
    Utf8Chars text(env, lfos);
    geode_loop_job_set_lfos(jobOf(job), text.get());
}

JNIEXPORT void JNICALL Java_dev_geode_engine_bridge_GeodeNative_loopJobSetAdsrs(JNIEnv* env, jobject, jlong job,
                                                                                jstring adsrs) {
    Utf8Chars text(env, adsrs);
    geode_loop_job_set_adsrs(jobOf(job), text.get());
}

JNIEXPORT void JNICALL Java_dev_geode_engine_bridge_GeodeNative_loopJobSetSoundTuning(JNIEnv* env, jobject, jlong job,
                                                                                      jfloatArray saved) {
    FloatElements data(env, saved);
    geode_loop_job_set_sound_tuning(jobOf(job), data.get(), data.length());
}

JNIEXPORT void JNICALL Java_dev_geode_engine_bridge_GeodeNative_loopJobSetReactions(JNIEnv* env, jobject, jlong job,
                                                                                    jstring reactions) {
    Utf8Chars text(env, reactions);
    geode_loop_job_set_reactions(jobOf(job), text.get());
}

JNIEXPORT void JNICALL Java_dev_geode_engine_bridge_GeodeNative_loopJobSetMotion(JNIEnv* env, jobject, jlong job,
                                                                                 jboolean reducedMotion,
                                                                                 jstring transitionId,
                                                                                 jlong transitionMs) {
    Utf8Chars id(env, transitionId);
    geode_loop_job_set_motion(jobOf(job), reducedMotion == JNI_TRUE ? 1 : 0, id.get(), transitionMs);
}

JNIEXPORT void JNICALL Java_dev_geode_engine_bridge_GeodeNative_loopJobSetUnderlay(JNIEnv* env, jobject, jlong job,
                                                                                   jintArray pixels, jint width,
                                                                                   jint height, jint blend,
                                                                                   jfloat amount) {
    jint* data = pixels ? env->GetIntArrayElements(pixels, nullptr) : nullptr;
    if (pixels && !data) return;
    geode_loop_job_set_underlay(jobOf(job), reinterpret_cast<const uint32_t*>(data),
                                pixels ? env->GetArrayLength(pixels) : 0, width, height, blend, amount);
    if (data) env->ReleaseIntArrayElements(pixels, data, JNI_ABORT);
}

// A direct buffer's address, or null for a heap buffer or one shorter than the tile (the job then carries no
// mark, and the shell fails the render rather than ship it unmarked).
JNIEXPORT jboolean JNICALL Java_dev_geode_engine_bridge_GeodeNative_loopJobSetMark(JNIEnv* env, jobject, jlong job,
                                                                                   jobject rgba, jint tileWidth,
                                                                                   jint tileHeight, jint placement,
                                                                                   jint corner) {
    const jlong needed = static_cast<jlong>(tileWidth) * static_cast<jlong>(tileHeight) * 4;
    auto* bytes = rgba ? static_cast<const uint8_t*>(env->GetDirectBufferAddress(rgba)) : nullptr;
    const bool whole = bytes && needed > 0 && env->GetDirectBufferCapacity(rgba) >= needed;
    geode_loop_job_set_mark(jobOf(job), whole ? bytes : nullptr, tileWidth, tileHeight, placement, corner);
    return whole ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT void JNICALL Java_dev_geode_engine_bridge_GeodeNative_loopJobRun(JNIEnv*, jobject, jlong job) {
    geode_loop_job_run(jobOf(job));
}

JNIEXPORT void JNICALL Java_dev_geode_engine_bridge_GeodeNative_loopJobCancel(JNIEnv*, jobject, jlong job) {
    geode_loop_job_cancel(jobOf(job));
}

JNIEXPORT jfloat JNICALL Java_dev_geode_engine_bridge_GeodeNative_loopJobProgress(JNIEnv*, jobject, jlong job) {
    return geode_loop_job_progress(jobOf(job));
}

JNIEXPORT jint JNICALL Java_dev_geode_engine_bridge_GeodeNative_loopJobState(JNIEnv*, jobject, jlong job) {
    return geode_loop_job_state(jobOf(job));
}

JNIEXPORT jint JNICALL Java_dev_geode_engine_bridge_GeodeNative_loopJobFailure(JNIEnv*, jobject, jlong job) {
    return geode_loop_job_failure(jobOf(job));
}

JNIEXPORT jstring JNICALL Java_dev_geode_engine_bridge_GeodeNative_loopJobDetail(JNIEnv* env, jobject, jlong job) {
    return geode::jni::utf8String(env, geode_loop_job_detail(jobOf(job)));
}

JNIEXPORT jint JNICALL Java_dev_geode_engine_bridge_GeodeNative_loopJobFps(JNIEnv*, jobject, jlong job) {
    return geode_loop_job_fps(jobOf(job));
}

JNIEXPORT jlong JNICALL Java_dev_geode_engine_bridge_GeodeNative_loopJobCrossfadeMs(JNIEnv*, jobject, jlong job) {
    return geode_loop_job_crossfade_ms(jobOf(job));
}

JNIEXPORT jint JNICALL Java_dev_geode_engine_bridge_GeodeNative_loopJobFileCount(JNIEnv*, jobject, jlong job) {
    return geode_loop_job_file_count(jobOf(job));
}

JNIEXPORT jstring JNICALL Java_dev_geode_engine_bridge_GeodeNative_loopJobFile(JNIEnv* env, jobject, jlong job,
                                                                               jint index) {
    const char* path = geode_loop_job_file(jobOf(job), index);
    return path ? geode::jni::utf8String(env, path) : nullptr;
}

}  // extern "C"
