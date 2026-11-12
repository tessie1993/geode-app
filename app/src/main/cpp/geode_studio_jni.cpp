#include <jni.h>

#include <memory>

#include "api/geode_studio_api.h"
#include "jni_util.hpp"

using geode::jni::Utf8Chars;
using geode::jni::utf8String;

namespace {

using Text = std::unique_ptr<geode_studio_text, decltype(&geode_studio_text_destroy)>;

jstring stringOf(JNIEnv* env, geode_studio_text* made) {
    const Text text(made, &geode_studio_text_destroy);
    return text ? utf8String(env, geode_studio_text_get(text.get())) : nullptr;
}

// A Java string that could not be read must not reach native as "absent": that would read as the empty document.
bool unread(jstring str, const Utf8Chars& chars) { return str && !chars.get(); }

}  // namespace

extern "C" {

JNIEXPORT jstring JNICALL
Java_dev_geode_engine_bridge_GeodeNative_studioEditRead(JNIEnv* env, jobject, jstring json) {
    const Utf8Chars chars(env, json);
    if (unread(json, chars)) return nullptr;
    return stringOf(env, geode_studio_edit_read(chars.get()));
}

JNIEXPORT jstring JNICALL
Java_dev_geode_engine_bridge_GeodeNative_studioKindRead(JNIEnv* env, jobject, jstring json) {
    const Utf8Chars chars(env, json);
    if (unread(json, chars)) return nullptr;
    return stringOf(env, geode_studio_kind_read(chars.get()));
}

JNIEXPORT jstring JNICALL
Java_dev_geode_engine_bridge_GeodeNative_studioEditNew(JNIEnv* env, jobject, jstring songUri, jstring songTitle,
                                                       jlong songMs, jstring sceneId, jstring milkPath,
                                                       jlong renderMs) {
    const Utf8Chars uri(env, songUri);
    const Utf8Chars title(env, songTitle);
    const Utf8Chars scene(env, sceneId);
    const Utf8Chars milk(env, milkPath);
    if (unread(songUri, uri) || unread(songTitle, title) || unread(sceneId, scene) || unread(milkPath, milk)) {
        return nullptr;
    }
    return stringOf(env, geode_studio_edit_new(uri.get(), title.get(), songMs, scene.get(), milk.get(), renderMs));
}

JNIEXPORT jlong JNICALL
Java_dev_geode_engine_bridge_GeodeNative_studioMinClipMs(JNIEnv*, jobject) {
    return geode_studio_min_clip_ms();
}

}  // extern "C"
