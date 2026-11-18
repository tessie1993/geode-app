// JNI marshalling for a video export (core/api/geode_video_export_api.h). Handles and values only: the job, its
// frame loop, its timing, its encoder and its muxer are core/output/VideoExport. The shell polls; nothing here
// calls back, so no JavaVM or global reference is kept. videoExportRun blocks its (worker) thread for the render.
#include <jni.h>

#include <android/asset_manager_jni.h>

#include <cstdint>
#include <vector>

#include "api/geode_video_export_api.h"
#include "jni_util.hpp"

namespace {

using geode::jni::Utf8Chars;

geode_video_export* jobOf(jlong handle) { return reinterpret_cast<geode_video_export*>(handle); }

}  // namespace

extern "C" {

JNIEXPORT jlong JNICALL Java_dev_geode_engine_bridge_VideoExportNative_videoExportCreate(JNIEnv*, jobject) {
    return reinterpret_cast<jlong>(geode_video_export_create());
}

JNIEXPORT void JNICALL Java_dev_geode_engine_bridge_VideoExportNative_videoExportDestroy(JNIEnv*, jobject, jlong job) {
    geode_video_export_destroy(jobOf(job));
}

JNIEXPORT void JNICALL Java_dev_geode_engine_bridge_VideoExportNative_videoExportSetOutput(JNIEnv* env, jobject, jlong job,
                                                                                     jint outputFd, jstring cacheDir,
                                                                                     jint width, jint height,
                                                                                     jint bitRate, jint fps) {
    Utf8Chars dir(env, cacheDir);
    geode_video_export_set_output(jobOf(job), outputFd, dir.get(), width, height, bitRate, fps);
}

JNIEXPORT void JNICALL Java_dev_geode_engine_bridge_VideoExportNative_videoExportAddCodec(JNIEnv* env, jobject, jlong job,
                                                                                    jstring mime, jint topFps) {
    Utf8Chars type(env, mime);
    geode_video_export_add_codec(jobOf(job), type.get(), topFps);
}

JNIEXPORT void JNICALL Java_dev_geode_engine_bridge_VideoExportNative_videoExportSetTrack(JNIEnv*, jobject, jlong job,
                                                                                    jint fd, jlong offset, jlong length,
                                                                                    jlong rangeStartMs) {
    geode_video_export_set_track(jobOf(job), fd, offset, length, rangeStartMs);
}

JNIEXPORT void JNICALL Java_dev_geode_engine_bridge_VideoExportNative_videoExportSetScene(JNIEnv* env, jobject, jlong job,
                                                                                    jobject assetManager,
                                                                                    jstring milkTextureDir,
                                                                                    jstring sceneId, jstring milkPath,
                                                                                    jboolean entitled) {
    Utf8Chars textures(env, milkTextureDir);
    Utf8Chars scene(env, sceneId);
    Utf8Chars milk(env, milkPath);
    geode_video_export_set_scene(jobOf(job), assetManager ? AAssetManager_fromJava(env, assetManager) : nullptr,
                                 textures.get(), scene.get(), milk.get(), entitled == JNI_TRUE ? 1 : 0);
}

// As vizSetLook: a look whose routes' text could not be read is not handed over.
JNIEXPORT void JNICALL Java_dev_geode_engine_bridge_VideoExportNative_videoExportSetLook(
    JNIEnv* env, jobject, jlong job, jfloatArray params, jstring lfos, jstring adsrs, jfloatArray tuning,
    jstring reactions, jboolean reducedMotion, jstring transitionId, jlong transitionMs) {
    geode::jni::LookArgs look(env, nullptr, params, lfos, adsrs, tuning, reactions, reducedMotion, transitionId,
                              transitionMs);
    if (look.ok()) geode_video_export_set_look(jobOf(job), look.get());
}

JNIEXPORT void JNICALL Java_dev_geode_engine_bridge_VideoExportNative_videoExportSetUnderlay(JNIEnv* env, jobject,
                                                                                       jlong job, jintArray pixels,
                                                                                       jint width, jint height,
                                                                                       jint blend, jfloat amount) {
    jint* data = pixels ? env->GetIntArrayElements(pixels, nullptr) : nullptr;
    geode_video_export_set_underlay(jobOf(job), reinterpret_cast<const uint32_t*>(data),
                                    pixels ? env->GetArrayLength(pixels) : 0, width, height, blend, amount);
    if (data) env->ReleaseIntArrayElements(pixels, data, JNI_ABORT);
}

// A direct buffer's address, or null for a heap buffer or one shorter than the tile (the job then carries no
// mark, and the shell fails the render rather than ship it unmarked).
JNIEXPORT jboolean JNICALL Java_dev_geode_engine_bridge_VideoExportNative_videoExportSetMark(JNIEnv* env, jobject, jlong job,
                                                                                       jobject rgba, jint tileWidth,
                                                                                       jint tileHeight, jint placement,
                                                                                       jint corner) {
    const jlong needed = static_cast<jlong>(tileWidth) * static_cast<jlong>(tileHeight) * 4;
    auto* bytes = rgba ? static_cast<const uint8_t*>(env->GetDirectBufferAddress(rgba)) : nullptr;
    const bool whole = bytes && needed > 0 && env->GetDirectBufferCapacity(rgba) >= needed;
    geode_video_export_set_mark(jobOf(job), whole ? bytes : nullptr, tileWidth, tileHeight, placement, corner);
    return whole ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jboolean JNICALL Java_dev_geode_engine_bridge_VideoExportNative_videoExportSetAudio(JNIEnv* env, jobject,
                                                                                        jlong job, jstring path,
                                                                                        jlongArray samples) {
    Utf8Chars file(env, path);
    const jsize length = samples ? env->GetArrayLength(samples) : 0;
    std::vector<int64_t> values(static_cast<size_t>(length));
    if (length > 0) env->GetLongArrayRegion(samples, 0, length, reinterpret_cast<jlong*>(values.data()));
    return geode_video_export_set_audio(jobOf(job), file.get(), values.data(), static_cast<int>(length / 4))
               ? JNI_TRUE
               : JNI_FALSE;
}

JNIEXPORT void JNICALL Java_dev_geode_engine_bridge_VideoExportNative_videoExportAddAudioKey(
    JNIEnv* env, jobject, jlong job, jstring key, jint type, jlong whole, jfloat real, jstring text,
    jbyteArray bytes) {
    Utf8Chars name(env, key);
    Utf8Chars value(env, text);
    const jsize length = bytes ? env->GetArrayLength(bytes) : 0;
    std::vector<uint8_t> data(static_cast<size_t>(length));
    if (length > 0) env->GetByteArrayRegion(bytes, 0, length, reinterpret_cast<jbyte*>(data.data()));
    geode_video_export_add_audio_key(jobOf(job), name.get(), type, whole, real, value.get(), data.data(),
                                     static_cast<int>(length));
}

JNIEXPORT void JNICALL Java_dev_geode_engine_bridge_VideoExportNative_videoExportRun(JNIEnv*, jobject, jlong job) {
    geode_video_export_run(jobOf(job));
}

JNIEXPORT void JNICALL Java_dev_geode_engine_bridge_VideoExportNative_videoExportCancel(JNIEnv*, jobject, jlong job) {
    geode_video_export_cancel(jobOf(job));
}

JNIEXPORT jfloat JNICALL Java_dev_geode_engine_bridge_VideoExportNative_videoExportProgress(JNIEnv*, jobject, jlong job) {
    return geode_video_export_progress(jobOf(job));
}

JNIEXPORT jfloat JNICALL Java_dev_geode_engine_bridge_VideoExportNative_videoExportLeadIn(JNIEnv*, jobject, jfloat done) {
    return geode_video_export_lead_in(done);
}

JNIEXPORT jint JNICALL Java_dev_geode_engine_bridge_VideoExportNative_videoExportState(JNIEnv*, jobject, jlong job) {
    return geode_video_export_state(jobOf(job));
}

JNIEXPORT jint JNICALL Java_dev_geode_engine_bridge_VideoExportNative_videoExportFailure(JNIEnv*, jobject, jlong job) {
    return geode_video_export_failure(jobOf(job));
}

JNIEXPORT jstring JNICALL Java_dev_geode_engine_bridge_VideoExportNative_videoExportDetail(JNIEnv* env, jobject, jlong job) {
    return geode::jni::utf8String(env, geode_video_export_detail(jobOf(job)));
}

}  // extern "C"
