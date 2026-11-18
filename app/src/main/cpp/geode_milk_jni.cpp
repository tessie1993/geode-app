#include <jni.h>
#include <android/asset_manager_jni.h>

#include <string>
#include <vector>

#include "api/geode_milk_api.h"
#include "jni_util.hpp"

using geode::jni::Utf8Chars;
using geode::jni::utf8Lines;
using geode::jni::utf8String;

namespace {

AAssetManager* assetsOf(JNIEnv* env, jobject assetManager) { return assetManager ? AAssetManager_fromJava(env, assetManager) : nullptr; }

std::vector<std::string> textsOf(JNIEnv* env, jobjectArray array) {
    const jsize count = array ? env->GetArrayLength(array) : 0;
    std::vector<std::string> texts;
    texts.reserve(static_cast<size_t>(count));
    for (jsize i = 0; i < count; ++i) {
        auto item = static_cast<jstring>(env->GetObjectArrayElement(array, i));
        {
            const Utf8Chars chars(env, item);
            texts.emplace_back(chars.get() ? chars.get() : "");
        }
        if (item) env->DeleteLocalRef(item);
    }
    return texts;
}

std::vector<const char*> pointersOf(const std::vector<std::string>& texts) {
    std::vector<const char*> ptrs;
    ptrs.reserve(texts.size());
    for (const auto& t : texts) ptrs.push_back(t.c_str());
    return ptrs;
}

}  // namespace

extern "C" {

JNIEXPORT void JNICALL
Java_dev_geode_engine_bridge_MilkNative_milkMemorySeed(JNIEnv* env, jobject, jstring saved) {
    const Utf8Chars path(env, saved);
    geode_milk_memory_seed(path.get());
}

JNIEXPORT void JNICALL
Java_dev_geode_engine_bridge_MilkNative_milkMemoryRequest(JNIEnv* env, jobject, jstring path) {
    const Utf8Chars chars(env, path);
    geode_milk_memory_request(chars.get());
}

JNIEXPORT jboolean JNICALL
Java_dev_geode_engine_bridge_MilkNative_milkMemoryLoaded(JNIEnv* env, jobject, jstring path) {
    const Utf8Chars chars(env, path);
    return geode_milk_memory_loaded(chars.get()) ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jboolean JNICALL
Java_dev_geode_engine_bridge_MilkNative_milkMemoryForget(JNIEnv* env, jobject, jobjectArray paths) {
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
Java_dev_geode_engine_bridge_MilkNative_milkMemoryLastShown(JNIEnv* env, jobject) {
    return utf8String(env, geode_milk_memory_last_shown());
}

JNIEXPORT jstring JNICALL
Java_dev_geode_engine_bridge_MilkNative_milkMemoryRestorePath(JNIEnv* env, jobject, jobject assetManager, jstring milkDir) {
    const Utf8Chars dir(env, milkDir);
    return utf8String(env, geode_milk_memory_restore_path(assetsOf(env, assetManager), dir.get()));
}

JNIEXPORT jint JNICALL
Java_dev_geode_engine_bridge_MilkNative_milkStarterInstall(JNIEnv* env, jobject, jobject assetManager, jstring milkDir) {
    const Utf8Chars dir(env, milkDir);
    return geode_milk_starter_install(assetsOf(env, assetManager), dir.get());
}

JNIEXPORT jlong JNICALL
Java_dev_geode_engine_bridge_MilkNative_milkTextureMaxBytes(JNIEnv*, jobject) {
    return static_cast<jlong>(geode_milk_texture_max_bytes());
}

JNIEXPORT jboolean JNICALL
Java_dev_geode_engine_bridge_MilkNative_milkTextureAdmits(JNIEnv* env, jobject, jstring name, jlong declaredBytes) {
    const Utf8Chars chars(env, name);
    return geode_milk_texture_admits(chars.get(), static_cast<long long>(declaredBytes)) ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jstring JNICALL
Java_dev_geode_engine_bridge_MilkNative_milkTextureStoredName(JNIEnv* env, jobject, jstring name, jbyteArray bytes, jint width,
                                                              jint height) {
    const Utf8Chars chars(env, name);
    const jsize length = bytes ? env->GetArrayLength(bytes) : 0;
    void* data = bytes ? env->GetPrimitiveArrayCritical(bytes, nullptr) : nullptr;
    const char* stored = geode_milk_texture_stored_name(chars.get(), static_cast<const unsigned char*>(data),
                                                        data ? static_cast<long long>(length) : 0, width, height);
    if (data) env->ReleasePrimitiveArrayCritical(bytes, data, JNI_ABORT);
    return utf8String(env, stored);
}

JNIEXPORT jobjectArray JNICALL
Java_dev_geode_engine_bridge_MilkNative_milkTextureList(JNIEnv* env, jobject, jstring textureDir) {
    const Utf8Chars dir(env, textureDir);
    return utf8Lines(env, geode_milk_texture_list(dir.get()));
}

JNIEXPORT jobjectArray JNICALL
Java_dev_geode_engine_bridge_MilkNative_milkTextureRemovedPresets(JNIEnv* env, jobject, jstring texture, jstring textureDir) {
    const Utf8Chars name(env, texture);
    const Utf8Chars dir(env, textureDir);
    return utf8Lines(env, geode_milk_texture_removed_presets(name.get(), dir.get()));
}

JNIEXPORT jstring JNICALL
Java_dev_geode_engine_bridge_MilkNative_milkTextureShowPresetName(JNIEnv* env, jobject, jstring texture) {
    const Utf8Chars name(env, texture);
    return utf8String(env, geode_milk_texture_show_preset_name(name.get()));
}

JNIEXPORT jstring JNICALL
Java_dev_geode_engine_bridge_MilkNative_milkTextureShowPresetText(JNIEnv* env, jobject, jstring texture) {
    const Utf8Chars name(env, texture);
    return utf8String(env, geode_milk_texture_show_preset_text(name.get()));
}

JNIEXPORT jobjectArray JNICALL
Java_dev_geode_engine_bridge_MilkNative_milkTextureLinks(JNIEnv* env, jobject, jstring presetPath, jstring textureDir,
                                                         jobjectArray overrideKeys, jobjectArray overrideValues) {
    const Utf8Chars preset(env, presetPath);
    const Utf8Chars dir(env, textureDir);
    const std::vector<std::string> keys = textsOf(env, overrideKeys);
    const std::vector<std::string> values = textsOf(env, overrideValues);
    const std::vector<const char*> keyPtrs = pointersOf(keys);
    const std::vector<const char*> valuePtrs = pointersOf(values);
    const int count = static_cast<int>(keys.size() < values.size() ? keys.size() : values.size());
    return utf8Lines(env, geode_milk_texture_links(preset.get(), dir.get(), keyPtrs.data(), valuePtrs.data(), count));
}

JNIEXPORT jobjectArray JNICALL
Java_dev_geode_engine_bridge_MilkNative_milkTextureLinkedPresets(JNIEnv* env, jobject, jstring milkDir) {
    const Utf8Chars dir(env, milkDir);
    return utf8Lines(env, geode_milk_texture_linked_presets(dir.get()));
}

JNIEXPORT jobjectArray JNICALL
Java_dev_geode_engine_bridge_MilkNative_milkTextureStaleLinkDirs(JNIEnv* env, jobject, jstring milkDir) {
    const Utf8Chars dir(env, milkDir);
    return utf8Lines(env, geode_milk_texture_stale_link_dirs(dir.get()));
}

JNIEXPORT jint JNICALL
Java_dev_geode_engine_bridge_MilkNative_milkPackEntryKind(JNIEnv* env, jobject, jstring name) {
    const Utf8Chars chars(env, name);
    return geode_milk_pack_entry_kind(chars.get());
}

JNIEXPORT jstring JNICALL
Java_dev_geode_engine_bridge_MilkNative_milkPackTargetName(JNIEnv* env, jobject, jstring name) {
    const Utf8Chars chars(env, name);
    return utf8String(env, geode_milk_pack_target_name(chars.get()));
}

JNIEXPORT jboolean JNICALL
Java_dev_geode_engine_bridge_MilkNative_milkPresetMissesTexture(JNIEnv* env, jobject, jstring presetPath, jstring textureDir) {
    const Utf8Chars preset(env, presetPath);
    const Utf8Chars dir(env, textureDir);
    return geode_milk_preset_misses_texture(preset.get(), dir.get()) ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jstring JNICALL
Java_dev_geode_engine_bridge_MilkNative_milkImportFileName(JNIEnv* env, jobject, jstring milkDir, jstring displayName,
                                                           jboolean keepExisting) {
    const Utf8Chars dir(env, milkDir);
    const Utf8Chars display(env, displayName);
    return utf8String(env, geode_milk_import_file_name(dir.get(), display.get(), keepExisting == JNI_TRUE ? 1 : 0));
}

}  // extern "C"
