// JNI for the shell's rules (core/shell through api/geode_shell_api.h): marshalling only.
#include <jni.h>

#include <vector>

#include "api/geode_shell_api.h"
#include "jni_util.hpp"

namespace {

jintArray intsOf(JNIEnv* env, const std::vector<int>& values) {
    const auto count = static_cast<jsize>(values.size());
    jintArray out = env->NewIntArray(count);
    if (out && count > 0) env->SetIntArrayRegion(out, 0, count, reinterpret_cast<const jint*>(values.data()));
    return out;
}

}  // namespace

extern "C" {

JNIEXPORT jintArray JNICALL Java_dev_geode_engine_bridge_ShellNative_exportFpsOptions(JNIEnv* env, jobject) {
    std::vector<int> options(static_cast<size_t>(geode_export_fps_options(nullptr, 0)));
    geode_export_fps_options(options.data(), static_cast<int>(options.size()));
    return intsOf(env, options);
}

JNIEXPORT jint JNICALL Java_dev_geode_engine_bridge_ShellNative_exportFpsRead(JNIEnv*, jobject, jint stored, jint fallback) {
    return geode_export_fps_read(stored, fallback);
}

}  // extern "C"
