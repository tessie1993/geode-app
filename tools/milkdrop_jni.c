#include <jni.h>
#include <string.h>
#include <stdio.h>
#include <android/log.h>
#include <projectM-4/projectM.h>

#define TAG "milkdrop-jni"
/* LOGI names user-chosen preset and texture paths, and native code cannot see
 * BuildConfig.DEBUG - so the gate that keeps RingLog's echo out of release
 * builds does not reach here. native-libs.yml compiles this file with -DNDEBUG,
 * so the shipped bridge carries none of these. Errors still log either way. */
#ifdef NDEBUG
#define LOGI(...) ((void) 0)
#else
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, TAG, __VA_ARGS__)
#endif
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, TAG, __VA_ARGS__)

/*
 * Minimal JNI bridge over the STOCK projectM 4 C API (v4.1.7, unpatched).
 *
 * There is deliberately no render-to-FBO entry point: upstream's
 * projectm_opengl_render_frame ends its frame on the DEFAULT framebuffer
 * (where its glDrawBuffers(GL_BACK) is legal), and the Kotlin scene copies
 * the result off framebuffer 0 into its own texture. Rendering into a
 * framebuffer object required patching the engine, and a stale or wrong
 * patch shipped a permanently black MilkDrop twice; the stock engine plus a
 * copy cannot drift that way.
 *
 * All GL-touching functions (create, render, load, destroy) must run on the
 * GL thread, which also means the error buffer needs no locking.
 */

static char g_last_error[512] = {0};

static void on_preset_switch_failed(const char *preset_filename, const char *message, void *user_data) {
    (void) user_data;
    snprintf(g_last_error, sizeof(g_last_error), "%s: %s",
             preset_filename ? preset_filename : "?", message ? message : "unknown error");
    LOGE("preset switch failed: %s", g_last_error);
}

JNIEXPORT jlong JNICALL
Java_dev_geode_render_scene_MilkdropEngine_nativeCreate(JNIEnv *env, jobject thiz) {
    projectm_handle h = projectm_create();
    if (h) {
        projectm_set_fps(h, 60);
        projectm_set_mesh_size(h, 48, 32);
        projectm_set_soft_cut_duration(h, 3.0);
        projectm_set_preset_duration(h, 999999.0); /* never auto-switch */
        projectm_set_preset_locked(h, true);
        projectm_set_aspect_correction(h, true);
        projectm_set_preset_switch_failed_event_callback(h, on_preset_switch_failed, NULL);
        LOGI("projectM instance created");
    } else {
        LOGE("projectm_create returned NULL");
    }
    return (jlong) h;
}

JNIEXPORT void JNICALL
Java_dev_geode_render_scene_MilkdropEngine_nativeDestroy(JNIEnv *env, jobject thiz, jlong handle) {
    if (handle) projectm_destroy((projectm_handle) handle);
}

JNIEXPORT void JNICALL
Java_dev_geode_render_scene_MilkdropEngine_nativeResize(JNIEnv *env, jobject thiz, jlong handle,
                                                        jint width, jint height) {
    if (handle) projectm_set_window_size((projectm_handle) handle, (size_t) width, (size_t) height);
}

JNIEXPORT void JNICALL
Java_dev_geode_render_scene_MilkdropEngine_nativeAddPcmMono(JNIEnv *env, jobject thiz, jlong handle,
                                                            jfloatArray samples, jint count) {
    if (!handle || !samples || count <= 0) return;
    /* Clamp to the array's real length: a Java-side caller bug must surface
     * as short audio, not as an out-of-bounds heap read inside projectM. */
    jsize len = (*env)->GetArrayLength(env, samples);
    if (count > len) count = len;
    if (count <= 0) return;
    jfloat *data = (*env)->GetFloatArrayElements(env, samples, NULL);
    if (data) {
        projectm_pcm_add_float((projectm_handle) handle, data, (unsigned int) count, PROJECTM_MONO);
        (*env)->ReleaseFloatArrayElements(env, samples, data, JNI_ABORT);
    }
}

JNIEXPORT void JNICALL
Java_dev_geode_render_scene_MilkdropEngine_nativeRender(JNIEnv *env, jobject thiz, jlong handle) {
    if (handle) projectm_opengl_render_frame((projectm_handle) handle);
}

JNIEXPORT void JNICALL
Java_dev_geode_render_scene_MilkdropEngine_nativeSetTexturePaths(JNIEnv *env, jobject thiz, jlong handle,
                                                                 jobjectArray dirs) {
    if (!handle || !dirs) return;
    jsize n = (*env)->GetArrayLength(env, dirs);
    if (n <= 0 || n > 8) return;
    const char *paths[8];
    /* What GetStringUTFChars actually returned, kept apart from paths[]:
     * it is NULL on OOM, and NULL must neither be logged, handed to
     * projectM, nor released (all three are UB). */
    const char *owned[8];
    jstring strs[8];
    for (jsize i = 0; i < n; i++) {
        strs[i] = (jstring) (*env)->GetObjectArrayElement(env, dirs, i);
        owned[i] = strs[i] ? (*env)->GetStringUTFChars(env, strs[i], NULL) : NULL;
        paths[i] = owned[i] ? owned[i] : "";
        LOGI("texture search path[%d]: %s", (int) i, paths[i]);
    }
    projectm_set_texture_search_paths((projectm_handle) handle, paths, (size_t) n);
    for (jsize i = 0; i < n; i++) {
        if (owned[i]) (*env)->ReleaseStringUTFChars(env, strs[i], owned[i]);
    }
}

JNIEXPORT void JNICALL
Java_dev_geode_render_scene_MilkdropEngine_nativeLoadPreset(JNIEnv *env, jobject thiz, jlong handle,
                                                            jstring path, jboolean smooth) {
    if (!handle || !path) return;
    const char *cpath = (*env)->GetStringUTFChars(env, path, NULL);
    if (cpath) {
        g_last_error[0] = '\0';
        LOGI("loading preset: %s (smooth=%d)", cpath, (int) smooth);
        projectm_load_preset_file((projectm_handle) handle, cpath, smooth);
        projectm_set_preset_locked((projectm_handle) handle, true);
        (*env)->ReleaseStringUTFChars(env, path, cpath);
    }
}

JNIEXPORT jstring JNICALL
Java_dev_geode_render_scene_MilkdropEngine_nativeGetLastError(JNIEnv *env, jobject thiz) {
    if (g_last_error[0] == '\0') return NULL;
    jstring result = (*env)->NewStringUTF(env, g_last_error);
    g_last_error[0] = '\0';
    return result;
}

JNIEXPORT void JNICALL
Java_dev_geode_render_scene_MilkdropEngine_nativeSetBeatSensitivity(JNIEnv *env, jobject thiz,
                                                                    jlong handle, jfloat value) {
    if (handle) projectm_set_beat_sensitivity((projectm_handle) handle, value);
}

JNIEXPORT void JNICALL
Java_dev_geode_render_scene_MilkdropEngine_nativeSetPresetLocked(JNIEnv *env, jobject thiz,
                                                                 jlong handle, jboolean locked) {
    if (handle) projectm_set_preset_locked((projectm_handle) handle, locked);
}
