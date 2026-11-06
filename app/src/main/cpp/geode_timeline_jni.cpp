#include <jni.h>

#include <algorithm>
#include <string>
#include <vector>

#include "api/geode_api.h"
#include "jni_util.hpp"

// A take's choices on song time (geode_take_timeline_*, #457): values only.
namespace {

geode_take_timeline* timelineOf(jlong handle) { return reinterpret_cast<geode_take_timeline*>(handle); }

}  // namespace

extern "C" {

JNIEXPORT jlong JNICALL
Java_dev_geode_engine_bridge_GeodeNative_takeTimelineCreate(JNIEnv*, jobject, jlong seed, jboolean songTime) {
    return reinterpret_cast<jlong>(geode_take_timeline_create(static_cast<uint64_t>(seed), songTime == JNI_TRUE ? 1 : 0));
}

JNIEXPORT jlong JNICALL
Java_dev_geode_engine_bridge_GeodeNative_takeTimelineDecode(JNIEnv* env, jobject, jstring text) {
    geode::jni::Utf8Chars chars(env, text);
    return reinterpret_cast<jlong>(geode_take_timeline_decode(chars.get()));
}

JNIEXPORT void JNICALL
Java_dev_geode_engine_bridge_GeodeNative_takeTimelineDestroy(JNIEnv*, jobject, jlong handle) {
    geode_take_timeline_destroy(timelineOf(handle));
}

JNIEXPORT void JNICALL
Java_dev_geode_engine_bridge_GeodeNative_takeTimelineNote(JNIEnv* env, jobject, jlong handle, jlong atMs, jstring sceneId,
                                                           jstring milkPath, jfloatArray params) {
    geode::jni::Utf8Chars scene(env, sceneId);
    geode::jni::Utf8Chars milk(env, milkPath);
    geode::jni::FloatElements values(env, params);
    geode_take_timeline_note(timelineOf(handle), atMs, scene.get(), milk.get(), values.get(), values.length());
}

JNIEXPORT jboolean JNICALL
Java_dev_geode_engine_bridge_GeodeNative_takeTimelineFull(JNIEnv*, jobject, jlong handle) {
    return geode_take_timeline_full(timelineOf(handle)) != 0 ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jstring JNICALL
Java_dev_geode_engine_bridge_GeodeNative_takeTimelineEncode(JNIEnv* env, jobject, jlong handle) {
    const size_t n = geode_take_timeline_encode(timelineOf(handle), nullptr, 0);
    std::vector<char> buf(n + 1, '\0');
    geode_take_timeline_encode(timelineOf(handle), buf.data(), buf.size());
    return env->NewStringUTF(buf.data());
}

JNIEXPORT jlong JNICALL
Java_dev_geode_engine_bridge_GeodeNative_takeTimelineSeed(JNIEnv*, jobject, jlong handle) {
    return static_cast<jlong>(geode_take_timeline_seed(timelineOf(handle)));
}

JNIEXPORT jboolean JNICALL
Java_dev_geode_engine_bridge_GeodeNative_takeTimelineRendersAgain(JNIEnv*, jobject, jlong handle) {
    return geode_take_timeline_renders_again(timelineOf(handle)) != 0 ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jlong JNICALL
Java_dev_geode_engine_bridge_GeodeNative_takeTimelineEndMs(JNIEnv*, jobject, jlong handle) {
    return geode_take_timeline_end_ms(timelineOf(handle));
}

JNIEXPORT jint JNICALL
Java_dev_geode_engine_bridge_GeodeNative_takeTimelineStateAt(JNIEnv* env, jobject, jlong handle, jlong ms, jfloatArray params) {
    const jsize n = params ? env->GetArrayLength(params) : 0;
    std::vector<float> values(static_cast<size_t>(n));
    const int index = geode_take_timeline_state_at(timelineOf(handle), ms, values.data(), n);
    if (index >= 0 && n > 0) env->SetFloatArrayRegion(params, 0, n, values.data());
    return index;
}

JNIEXPORT jlong JNICALL
Java_dev_geode_engine_bridge_GeodeNative_takeTimelineChoiceAtMs(JNIEnv*, jobject, jlong handle, jint index) {
    return geode_take_timeline_choice_at_ms(timelineOf(handle), index);
}

JNIEXPORT jstring JNICALL
Java_dev_geode_engine_bridge_GeodeNative_takeTimelineChoiceStyle(JNIEnv* env, jobject, jlong handle, jint index) {
    const size_t n = geode_take_timeline_choice_style(timelineOf(handle), index, nullptr, 0);
    std::vector<char> buf(n + 1, '\0');
    geode_take_timeline_choice_style(timelineOf(handle), index, buf.data(), buf.size());
    return env->NewStringUTF(buf.data());
}

JNIEXPORT jintArray JNICALL
Java_dev_geode_engine_bridge_GeodeNative_takeTimelineStyleChanges(JNIEnv* env, jobject, jlong handle) {
    const int n = geode_take_timeline_style_changes(timelineOf(handle), nullptr, 0);
    std::vector<int> indices(static_cast<size_t>(std::max(n, 0)));
    geode_take_timeline_style_changes(timelineOf(handle), indices.data(), n);
    jintArray out = env->NewIntArray(n);
    if (out && n > 0) env->SetIntArrayRegion(out, 0, n, reinterpret_cast<const jint*>(indices.data()));
    return out;
}

// Render again (#457): an export renderer replays the take natively; see geode_viz_set_take.
JNIEXPORT void JNICALL
Java_dev_geode_engine_bridge_GeodeNative_vizSetTake(JNIEnv*, jobject, jlong viz, jlong take, jlong songStartMs) {
    geode_viz_set_take(reinterpret_cast<geode_viz*>(viz), timelineOf(take), static_cast<int64_t>(songStartMs));
}

JNIEXPORT jint JNICALL
Java_dev_geode_engine_bridge_GeodeNative_vizTakeFrame(JNIEnv*, jobject, jlong viz, jlong renderMs) {
    return geode_viz_take_frame(reinterpret_cast<geode_viz*>(viz), static_cast<int64_t>(renderMs));
}

}
