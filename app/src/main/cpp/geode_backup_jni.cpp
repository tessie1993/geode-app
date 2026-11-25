// JNI for the backup's rules (core/backup through api/geode_api.h): each plan is one String[] in and one String[] out.
// Marshals only.
#include <jni.h>

#include <string>
#include <vector>

#include "api/geode_api.h"
#include "jni_util.hpp"

namespace {

using geode::jni::Utf8Chars;
using geode::jni::utf8String;

// A Java String[] as UTF-8 C strings: `pointers` point into `owned`. A null array is none; a null entry reads as "".
struct Texts {
    std::vector<std::string> owned;
    std::vector<const char*> pointers;
    int count() const { return static_cast<int>(pointers.size()); }
};

Texts textsOf(JNIEnv* env, jobjectArray array) {
    Texts out;
    const jsize n = array && !env->ExceptionCheck() ? env->GetArrayLength(array) : 0;
    out.owned.reserve(static_cast<size_t>(n));
    for (jsize i = 0; i < n; ++i) {
        auto* item = static_cast<jstring>(env->GetObjectArrayElement(array, i));
        Utf8Chars chars(env, item);
        out.owned.emplace_back(chars.get() ? chars.get() : "");
        env->DeleteLocalRef(item);
        if (env->ExceptionCheck()) break;  // out of memory: the caller's Java frame throws, nothing more is read
    }
    for (const auto& s : out.owned) out.pointers.push_back(s.c_str());
    return out;
}

// The answer as a Java String[]; null when out of memory.
jobjectArray answerOf(JNIEnv* env, const char* const* items, int count) {
    if (env->ExceptionCheck()) return nullptr;
    jclass string = env->FindClass("java/lang/String");
    jobjectArray out = string ? env->NewObjectArray(count, string, nullptr) : nullptr;
    for (int i = 0; out && i < count; ++i) {
        jstring item = utf8String(env, items[i]);
        if (!item) return nullptr;
        env->SetObjectArrayElement(out, i, item);
        env->DeleteLocalRef(item);
    }
    env->DeleteLocalRef(string);
    return out;
}

}  // namespace

extern "C" {

JNIEXPORT jobjectArray JNICALL Java_dev_geode_engine_bridge_BackupNative_layout(JNIEnv* env, jobject) {
    int n = 0;
    const char* const* items = geode_backup_layout(&n);
    return answerOf(env, items, n);
}

JNIEXPORT jobjectArray JNICALL Java_dev_geode_engine_bridge_BackupNative_planExport(JNIEnv* env, jobject, jobjectArray prefs,
                                                                                jobjectArray files) {
    const Texts p = textsOf(env, prefs);
    const Texts f = textsOf(env, files);
    int n = 0;
    const char* const* items = geode_backup_plan_export(p.pointers.data(), p.count(), f.pointers.data(), f.count(), &n);
    return answerOf(env, items, n);
}

JNIEXPORT jobjectArray JNICALL Java_dev_geode_engine_bridge_BackupNative_finishExport(JNIEnv* env, jobject, jobjectArray entries,
                                                                                  jint appVersionCode, jlong createdAtMs) {
    const Texts e = textsOf(env, entries);
    int n = 0;
    const char* const* items = geode_backup_finish_export(e.pointers.data(), e.count(), appVersionCode, createdAtMs, &n);
    return answerOf(env, items, n);
}

JNIEXPORT jobjectArray JNICALL Java_dev_geode_engine_bridge_BackupNative_planUnpack(JNIEnv* env, jobject, jobjectArray request) {
    const Texts r = textsOf(env, request);
    int n = 0;
    const char* const* items = geode_backup_plan_unpack(r.pointers.data(), r.count(), &n);
    return answerOf(env, items, n);
}

JNIEXPORT jobjectArray JNICALL Java_dev_geode_engine_bridge_BackupNative_checkStaging(JNIEnv* env, jobject, jobjectArray request) {
    const Texts r = textsOf(env, request);
    int n = 0;
    const char* const* items = geode_backup_check_staging(r.pointers.data(), r.count(), &n);
    return answerOf(env, items, n);
}

JNIEXPORT jobjectArray JNICALL Java_dev_geode_engine_bridge_BackupNative_checkExtracted(JNIEnv* env, jobject, jobjectArray item) {
    const Texts r = textsOf(env, item);
    int n = 0;
    const char* const* items = geode_backup_check_extracted(r.pointers.data(), r.count(), &n);
    return answerOf(env, items, n);
}

JNIEXPORT jobjectArray JNICALL Java_dev_geode_engine_bridge_BackupNative_finishUnpack(JNIEnv* env, jobject, jobjectArray request) {
    const Texts r = textsOf(env, request);
    int n = 0;
    const char* const* items = geode_backup_finish_unpack(r.pointers.data(), r.count(), &n);
    return answerOf(env, items, n);
}

JNIEXPORT jobjectArray JNICALL Java_dev_geode_engine_bridge_BackupNative_planRestore(JNIEnv* env, jobject, jobjectArray request) {
    const Texts r = textsOf(env, request);
    int n = 0;
    const char* const* items = geode_backup_plan_restore(r.pointers.data(), r.count(), &n);
    return answerOf(env, items, n);
}

JNIEXPORT jstring JNICALL Java_dev_geode_engine_bridge_BackupNative_fileName(JNIEnv* env, jobject, jint year, jint month, jint day) {
    return utf8String(env, geode_backup_file_name(year, month, day));
}

}  // extern "C"
