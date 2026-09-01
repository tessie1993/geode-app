#include <jni.h>
#include <android/log.h>
#include <projectM-4/projectM.h>

#include <array>
#include <cstdio>

namespace {

constexpr const char* kTag = "milkdrop-jni";

#ifdef NDEBUG
#define LOGI(...) ((void) 0)
#else
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, kTag, __VA_ARGS__)
#endif
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, kTag, __VA_ARGS__)

// Stock engine: projectm_opengl_render_frame ends on the default framebuffer and
// MilkdropScene copies it off framebuffer 0, so no render-to-FBO patch can go stale.
// Every GL-touching export runs on the GL thread, which is why the error buffer needs no lock.
std::array<char, 512> g_last_error{};

class Utf8Chars {
public:
    Utf8Chars(JNIEnv* env, jstring str) : env_(env), str_(str), chars_(str ? env->GetStringUTFChars(str, nullptr) : nullptr) {}
    ~Utf8Chars() { if (chars_) env_->ReleaseStringUTFChars(str_, chars_); }
    Utf8Chars(const Utf8Chars&) = delete;
    Utf8Chars& operator=(const Utf8Chars&) = delete;
    const char* get() const { return chars_; }

private:
    JNIEnv* env_;
    jstring str_;
    const char* chars_;
};

class FloatElements {
public:
    FloatElements(JNIEnv* env, jfloatArray array) : env_(env), array_(array), data_(env->GetFloatArrayElements(array, nullptr)) {}
    ~FloatElements() { if (data_) env_->ReleaseFloatArrayElements(array_, data_, JNI_ABORT); }
    FloatElements(const FloatElements&) = delete;
    FloatElements& operator=(const FloatElements&) = delete;
    const jfloat* get() const { return data_; }

private:
    JNIEnv* env_;
    jfloatArray array_;
    jfloat* data_;
};

void on_preset_switch_failed(const char* preset_filename, const char* message, void*) {
    std::snprintf(g_last_error.data(), g_last_error.size(), "%s: %s",
                  preset_filename ? preset_filename : "?", message ? message : "unknown error");
    LOGE("preset switch failed: %s", g_last_error.data());
}

projectm_handle as_handle(jlong handle) {
    return reinterpret_cast<projectm_handle>(handle);
}

}  // namespace

extern "C" {

JNIEXPORT jlong JNICALL
Java_dev_geode_render_scene_MilkdropEngine_nativeCreate(JNIEnv*, jobject) {
    projectm_handle h = projectm_create();
    if (h) {
        projectm_set_fps(h, 60);
        projectm_set_mesh_size(h, 48, 32);
        projectm_set_soft_cut_duration(h, 3.0);
        projectm_set_preset_duration(h, 999999.0);
        projectm_set_preset_locked(h, true);
        projectm_set_aspect_correction(h, true);
        projectm_set_preset_switch_failed_event_callback(h, on_preset_switch_failed, nullptr);
        LOGI("projectM instance created");
    } else {
        LOGE("projectm_create returned NULL");
    }
    return reinterpret_cast<jlong>(h);
}

JNIEXPORT void JNICALL
Java_dev_geode_render_scene_MilkdropEngine_nativeDestroy(JNIEnv*, jobject, jlong handle) {
    if (handle) projectm_destroy(as_handle(handle));
}

JNIEXPORT void JNICALL
Java_dev_geode_render_scene_MilkdropEngine_nativeResize(JNIEnv*, jobject, jlong handle, jint width, jint height) {
    if (handle) projectm_set_window_size(as_handle(handle), static_cast<size_t>(width), static_cast<size_t>(height));
}

JNIEXPORT void JNICALL
Java_dev_geode_render_scene_MilkdropEngine_nativeAddPcmMono(JNIEnv* env, jobject, jlong handle,
                                                            jfloatArray samples, jint count) {
    if (!handle || !samples || count <= 0) return;
    const jsize len = env->GetArrayLength(samples);
    if (count > len) count = len;
    if (count <= 0) return;
    FloatElements data(env, samples);
    if (data.get()) {
        projectm_pcm_add_float(as_handle(handle), data.get(), static_cast<unsigned int>(count), PROJECTM_MONO);
    }
}

JNIEXPORT void JNICALL
Java_dev_geode_render_scene_MilkdropEngine_nativeRender(JNIEnv*, jobject, jlong handle) {
    if (handle) projectm_opengl_render_frame(as_handle(handle));
}

JNIEXPORT void JNICALL
Java_dev_geode_render_scene_MilkdropEngine_nativeSetTexturePaths(JNIEnv* env, jobject, jlong handle,
                                                                 jobjectArray dirs) {
    if (!handle || !dirs) return;
    const jsize n = env->GetArrayLength(dirs);
    if (n <= 0 || n > 8) return;
    std::array<const char*, 8> paths{};
    std::array<jstring, 8> strs{};
    std::array<const char*, 8> owned{};
    for (jsize i = 0; i < n; i++) {
        strs[i] = static_cast<jstring>(env->GetObjectArrayElement(dirs, i));
        owned[i] = strs[i] ? env->GetStringUTFChars(strs[i], nullptr) : nullptr;
        paths[i] = owned[i] ? owned[i] : "";
        LOGI("texture search path[%d]: %s", static_cast<int>(i), paths[i]);
    }
    projectm_set_texture_search_paths(as_handle(handle), paths.data(), static_cast<size_t>(n));
    for (jsize i = 0; i < n; i++) {
        if (owned[i]) env->ReleaseStringUTFChars(strs[i], owned[i]);
    }
}

JNIEXPORT void JNICALL
Java_dev_geode_render_scene_MilkdropEngine_nativeLoadPreset(JNIEnv* env, jobject, jlong handle,
                                                            jstring path, jboolean smooth) {
    if (!handle || !path) return;
    Utf8Chars cpath(env, path);
    if (cpath.get()) {
        g_last_error[0] = '\0';
        LOGI("loading preset: %s (smooth=%d)", cpath.get(), static_cast<int>(smooth));
        projectm_load_preset_file(as_handle(handle), cpath.get(), smooth);
        projectm_set_preset_locked(as_handle(handle), true);
    }
}

JNIEXPORT jstring JNICALL
Java_dev_geode_render_scene_MilkdropEngine_nativeGetLastError(JNIEnv* env, jobject) {
    if (g_last_error[0] == '\0') return nullptr;
    jstring result = env->NewStringUTF(g_last_error.data());
    g_last_error[0] = '\0';
    return result;
}

JNIEXPORT void JNICALL
Java_dev_geode_render_scene_MilkdropEngine_nativeSetBeatSensitivity(JNIEnv*, jobject, jlong handle, jfloat value) {
    if (handle) projectm_set_beat_sensitivity(as_handle(handle), value);
}

JNIEXPORT void JNICALL
Java_dev_geode_render_scene_MilkdropEngine_nativeSetPresetLocked(JNIEnv*, jobject, jlong handle, jboolean locked) {
    if (handle) projectm_set_preset_locked(as_handle(handle), locked);
}

}
