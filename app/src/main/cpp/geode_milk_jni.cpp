#include <jni.h>
#include <android/asset_manager_jni.h>

#include <string>
#include <vector>

#include "api/geode_milk_api.h"
#include "jni_util.hpp"

using geode::jni::Utf8Chars;
using geode::jni::utf8String;

namespace {

AAssetManager* assetsOf(JNIEnv* env, jobject assetManager) { return assetManager ? AAssetManager_fromJava(env, assetManager) : nullptr; }

}  // namespace

extern "C" {

JNIEXPORT void JNICALL
Java_dev_geode_engine_bridge_GeodeNative_milkMemorySeed(JNIEnv* env, jobject, jstring saved) {
    const Utf8Chars path(env, saved);
    geode_milk_memory_seed(path.get());
}

JNIEXPORT void JNICALL
Java_dev_geode_engine_bridge_GeodeNative_milkMemoryRequest(JNIEnv* env, jobject, jstring path) {
    const Utf8Chars chars(env, path);
    geode_milk_memory_request(chars.get());
}

JNIEXPORT jboolean JNICALL
Java_dev_geode_engine_bridge_GeodeNative_milkMemoryLoaded(JNIEnv* env, jobject, jstring path) {
    const Utf8Chars chars(env, path);
    return geode_milk_memory_loaded(chars.get()) ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jboolean JNICALL
Java_dev_geode_engine_bridge_GeodeNative_milkMemoryForget(JNIEnv* env, jobject, jobjectArray paths) {
    const jsize count = paths ? env->GetArrayLength(paths) : 0;
    std::vector<std::string> texts;
    texts.reserve(static_cast<size_t>(count));
    for (jsize i = 0; i < count; ++i) {
        auto path = static_cast<jstring>(env->GetObjectArrayElement(paths, i));
        {
            const Utf8Chars chars(env, path);
            texts.emplace_back(chars.get() ? chars.get() : "");
        }
        if (path) env->DeleteLocalRef(path);
    }
    std::vector<const char*> ptrs;
    ptrs.reserve(texts.size());
    for (const auto& t : texts) ptrs.push_back(t.c_str());
    return geode_milk_memory_forget(ptrs.data(), static_cast<int>(ptrs.size())) ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jstring JNICALL
Java_dev_geode_engine_bridge_GeodeNative_milkMemoryLastShown(JNIEnv* env, jobject) {
    return utf8String(env, geode_milk_memory_last_shown());
}

JNIEXPORT jstring JNICALL
Java_dev_geode_engine_bridge_GeodeNative_milkMemoryRestorePath(JNIEnv* env, jobject, jobject assetManager, jstring milkDir) {
    const Utf8Chars dir(env, milkDir);
    return utf8String(env, geode_milk_memory_restore_path(assetsOf(env, assetManager), dir.get()));
}

JNIEXPORT jint JNICALL
Java_dev_geode_engine_bridge_GeodeNative_milkStarterInstall(JNIEnv* env, jobject, jobject assetManager, jstring milkDir) {
    const Utf8Chars dir(env, milkDir);
    return geode_milk_starter_install(assetsOf(env, assetManager), dir.get());
}

}  // extern "C"
