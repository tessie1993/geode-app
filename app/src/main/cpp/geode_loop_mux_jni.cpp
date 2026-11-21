// JNI marshalling for a loop extend's copy (core/api/geode_loop_mux_api.h). Handles and values only: the copy, its
// sample times, its interleave and its muxer are core/output/LoopMux. The shell polls; nothing here calls back, so
// no JavaVM or global reference is kept. loopMuxRun blocks its (worker) thread for the copy.
#include <jni.h>

#include <cstdint>

#include "api/geode_loop_mux_api.h"
#include "jni_util.hpp"

namespace {

using geode::jni::Utf8Chars;

geode_loop_mux* muxOf(jlong handle) { return reinterpret_cast<geode_loop_mux*>(handle); }

}  // namespace

extern "C" {

JNIEXPORT jlong JNICALL Java_dev_geode_engine_bridge_LoopMuxNative_loopMuxCreate(JNIEnv*, jobject) {
    return reinterpret_cast<jlong>(geode_loop_mux_create());
}

JNIEXPORT void JNICALL Java_dev_geode_engine_bridge_LoopMuxNative_loopMuxDestroy(JNIEnv*, jobject, jlong mux) {
    geode_loop_mux_destroy(muxOf(mux));
}

JNIEXPORT void JNICALL Java_dev_geode_engine_bridge_LoopMuxNative_loopMuxAddLoop(JNIEnv* env, jobject, jlong mux,
                                                                                 jstring path) {
    Utf8Chars file(env, path);
    geode_loop_mux_add_loop(muxOf(mux), file.get());
}

JNIEXPORT jint JNICALL Java_dev_geode_engine_bridge_LoopMuxNative_loopMuxAddClip(JNIEnv* env, jobject, jlong mux,
                                                                                 jlong aacJob, jstring path) {
    Utf8Chars file(env, path);
    return geode_loop_mux_add_clip(muxOf(mux), reinterpret_cast<const geode_aac_job*>(aacJob), file.get());
}

JNIEXPORT jlong JNICALL Java_dev_geode_engine_bridge_LoopMuxNative_loopMuxTotalUs(JNIEnv*, jobject, jlong mux) {
    return geode_loop_mux_total_us(muxOf(mux));
}

JNIEXPORT void JNICALL Java_dev_geode_engine_bridge_LoopMuxNative_loopMuxRun(JNIEnv*, jobject, jlong mux, jint outputFd,
                                                                             jlong loopDurationUs) {
    geode_loop_mux_run(muxOf(mux), outputFd, loopDurationUs);
}

JNIEXPORT void JNICALL Java_dev_geode_engine_bridge_LoopMuxNative_loopMuxCancel(JNIEnv*, jobject, jlong mux) {
    geode_loop_mux_cancel(muxOf(mux));
}

JNIEXPORT jfloat JNICALL Java_dev_geode_engine_bridge_LoopMuxNative_loopMuxProgress(JNIEnv*, jobject, jlong mux) {
    return geode_loop_mux_progress(muxOf(mux));
}

JNIEXPORT jint JNICALL Java_dev_geode_engine_bridge_LoopMuxNative_loopMuxState(JNIEnv*, jobject, jlong mux) {
    return geode_loop_mux_state(muxOf(mux));
}

JNIEXPORT jint JNICALL Java_dev_geode_engine_bridge_LoopMuxNative_loopMuxFailure(JNIEnv*, jobject, jlong mux) {
    return geode_loop_mux_failure(muxOf(mux));
}

JNIEXPORT jstring JNICALL Java_dev_geode_engine_bridge_LoopMuxNative_loopMuxDetail(JNIEnv* env, jobject, jlong mux) {
    return geode::jni::utf8String(env, geode_loop_mux_detail(muxOf(mux)));
}

}  // extern "C"
