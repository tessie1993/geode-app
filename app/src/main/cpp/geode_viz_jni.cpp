#include <jni.h>
#include <android/asset_manager_jni.h>

#include <cstring>
#include <string>
#include <vector>

#include "api/geode_api.h"
#include "jni_util.hpp"

namespace {

using geode::jni::FloatElements;
using geode::jni::Utf8Chars;

geode_viz* vizOf(jlong handle) { return reinterpret_cast<geode_viz*>(handle); }

}  // namespace

extern "C" {

JNIEXPORT jlong JNICALL
Java_dev_geode_engine_bridge_GeodeNative_vizCreate(JNIEnv* env, jobject, jobject assetManager, jstring cacheDir) {
    Utf8Chars dir(env, cacheDir);
    AAssetManager* assets = assetManager ? AAssetManager_fromJava(env, assetManager) : nullptr;
    return reinterpret_cast<jlong>(geode_viz_create(assets, dir.get()));
}

JNIEXPORT void JNICALL
Java_dev_geode_engine_bridge_GeodeNative_vizDestroy(JNIEnv*, jobject, jlong handle) {
    geode_viz_destroy(vizOf(handle));
}

JNIEXPORT jstring JNICALL
Java_dev_geode_engine_bridge_GeodeNative_vizParamNames(JNIEnv* env, jobject) {
    std::string joined;
    for (int i = 0; i < geode_viz_param_count(); ++i) {
        if (i > 0) joined += '\n';
        joined += geode_viz_param_name(i);
    }
    return env->NewStringUTF(joined.c_str());
}

JNIEXPORT void JNICALL
Java_dev_geode_engine_bridge_GeodeNative_vizSetParams(JNIEnv* env, jobject, jlong handle, jfloatArray values) {
    FloatElements data(env, values);
    if (data.get()) geode_viz_set_params(vizOf(handle), data.get(), data.length());
}

JNIEXPORT jboolean JNICALL
Java_dev_geode_engine_bridge_GeodeNative_vizSetParam(JNIEnv* env, jobject, jlong handle, jstring name, jfloat value) {
    Utf8Chars key(env, name);
    return geode_viz_set_param(vizOf(handle), key.get(), value) ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT void JNICALL
Java_dev_geode_engine_bridge_GeodeNative_vizSetFeatures(JNIEnv* env, jobject, jlong handle, jfloatArray frame) {
    FloatElements data(env, frame);
    if (!data.get() || data.length() < GEODE_FEATURE_FRAME_FLOATS) return;
    GeodeFeatureFrame features{};
    std::memcpy(&features, data.get(), sizeof(features));
    geode_viz_set_features(vizOf(handle), &features);
}

JNIEXPORT void JNICALL
Java_dev_geode_engine_bridge_GeodeNative_vizSetReducedMotion(JNIEnv*, jobject, jlong handle, jboolean on) {
    geode_viz_set_reduced_motion(vizOf(handle), on == JNI_TRUE);
}

JNIEXPORT void JNICALL
Java_dev_geode_engine_bridge_GeodeNative_vizSetLayer(JNIEnv* env, jobject, jlong handle, jstring sceneId, jfloat mix,
                                                     jint blendMode) {
    Utf8Chars id(env, sceneId);
    geode_viz_set_layer(vizOf(handle), id.get() ? id.get() : "", mix, blendMode);
}

JNIEXPORT void JNICALL
Java_dev_geode_engine_bridge_GeodeNative_vizSetTransition(JNIEnv* env, jobject, jlong handle, jstring id, jlong durationMs) {
    Utf8Chars name(env, id);
    geode_viz_set_transition(vizOf(handle), name.get(), durationMs);
}

JNIEXPORT void JNICALL
Java_dev_geode_engine_bridge_GeodeNative_vizBeginParamMorph(JNIEnv*, jobject, jlong handle, jfloat seconds) {
    geode_viz_begin_param_morph(vizOf(handle), seconds);
}

JNIEXPORT void JNICALL
Java_dev_geode_engine_bridge_GeodeNative_vizSetTouch(JNIEnv* env, jobject, jlong handle, jfloatArray xy, jint points) {
    FloatElements data(env, xy);
    const jint available = data.get() ? data.length() / 2 : 0;
    geode_viz_set_touch(vizOf(handle), data.get(), points < available ? points : available);
}

JNIEXPORT void JNICALL
Java_dev_geode_engine_bridge_GeodeNative_vizPushPcm(JNIEnv* env, jobject, jlong handle, jfloatArray mono, jint count) {
    FloatElements data(env, mono);
    if (!data.get()) return;
    geode_viz_push_pcm(vizOf(handle), data.get(), count < data.length() ? count : data.length());
}

JNIEXPORT void JNICALL
Java_dev_geode_engine_bridge_GeodeNative_vizSetCustomShader(JNIEnv* env, jobject, jlong handle, jstring sceneId,
                                                            jstring fragmentSource) {
    Utf8Chars id(env, sceneId);
    Utf8Chars source(env, fragmentSource);
    geode_viz_set_custom_shader(vizOf(handle), id.get(), source.get());
}

JNIEXPORT void JNICALL
Java_dev_geode_engine_bridge_GeodeNative_vizSetLfo(JNIEnv* env, jobject, jlong handle, jint slot, jfloatArray config) {
    FloatElements data(env, config);
    if (data.get()) geode_viz_set_lfo(vizOf(handle), slot, data.get(), data.length());
}

JNIEXPORT void JNICALL
Java_dev_geode_engine_bridge_GeodeNative_vizSetAdsr(JNIEnv* env, jobject, jlong handle, jint slot, jfloatArray config) {
    FloatElements data(env, config);
    if (data.get()) geode_viz_set_adsr(vizOf(handle), slot, data.get(), data.length());
}

JNIEXPORT void JNICALL
Java_dev_geode_engine_bridge_GeodeNative_vizSetThermal(JNIEnv*, jobject, jlong handle, jint status, jfloat headroom) {
    geode_viz_set_thermal(vizOf(handle), status, headroom);
}

JNIEXPORT void JNICALL
Java_dev_geode_engine_bridge_GeodeNative_vizSetPacedFps(JNIEnv*, jobject, jlong handle, jfloat fps) {
    geode_viz_set_paced_fps(vizOf(handle), fps);
}

JNIEXPORT void JNICALL
Java_dev_geode_engine_bridge_GeodeNative_vizSetOffscreen(JNIEnv*, jobject, jlong handle, jboolean on) {
    geode_viz_set_offscreen(vizOf(handle), on == JNI_TRUE);
}

JNIEXPORT jboolean JNICALL
Java_dev_geode_engine_bridge_GeodeNative_vizKnows(JNIEnv* env, jobject, jlong handle, jstring sceneId) {
    Utf8Chars id(env, sceneId);
    return geode_viz_knows(vizOf(handle), id.get()) ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jstring JNICALL
Java_dev_geode_engine_bridge_GeodeNative_vizSceneIds(JNIEnv* env, jobject, jlong handle) {
    const size_t length = geode_viz_scene_ids(vizOf(handle), nullptr, 0);
    std::vector<char> buffer(length + 1, '\0');
    geode_viz_scene_ids(vizOf(handle), buffer.data(), buffer.size());
    return env->NewStringUTF(buffer.data());
}

JNIEXPORT jstring JNICALL
Java_dev_geode_engine_bridge_GeodeNative_vizLastError(JNIEnv* env, jobject, jlong handle) {
    return env->NewStringUTF(geode_viz_last_error(vizOf(handle)));
}

JNIEXPORT void JNICALL
Java_dev_geode_engine_bridge_GeodeNative_vizSurfaceCreated(JNIEnv*, jobject, jlong handle) {
    geode_viz_surface_created(vizOf(handle));
}

JNIEXPORT void JNICALL
Java_dev_geode_engine_bridge_GeodeNative_vizSurfaceChanged(JNIEnv*, jobject, jlong handle, jint width, jint height) {
    geode_viz_surface_changed(vizOf(handle), width, height);
}

JNIEXPORT jboolean JNICALL
Java_dev_geode_engine_bridge_GeodeNative_vizSetScene(JNIEnv* env, jobject, jlong handle, jstring sceneId) {
    Utf8Chars id(env, sceneId);
    return geode_viz_set_scene(vizOf(handle), id.get()) ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT void JNICALL
Java_dev_geode_engine_bridge_GeodeNative_vizWarmTransition(JNIEnv* env, jobject, jlong handle, jstring id) {
    Utf8Chars name(env, id);
    geode_viz_warm_transition(vizOf(handle), name.get());
}

JNIEXPORT void JNICALL
Java_dev_geode_engine_bridge_GeodeNative_vizCut(JNIEnv*, jobject, jlong handle) {
    geode_viz_cut(vizOf(handle));
}

JNIEXPORT void JNICALL
Java_dev_geode_engine_bridge_GeodeNative_vizRender(JNIEnv*, jobject, jlong handle, jdouble timeSeconds, jint targetFbo) {
    geode_viz_render(vizOf(handle), timeSeconds, static_cast<uint32_t>(targetFbo));
}

JNIEXPORT void JNICALL
Java_dev_geode_engine_bridge_GeodeNative_vizReleaseScenes(JNIEnv*, jobject, jlong handle) {
    geode_viz_release_scenes(vizOf(handle));
}

}
