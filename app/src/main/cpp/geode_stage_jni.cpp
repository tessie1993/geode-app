#include <jni.h>
#include <android/asset_manager_jni.h>
#include <android/native_window_jni.h>

#include <algorithm>
#include <vector>

#include "api/geode_stage_api.h"
#include "jni_util.hpp"

// The native stage's JNI (docs/agents/native-stage.md): values in, values out. The stage thread never calls up
// here; the shell polls. A handle that is gone is the C API's to ignore.
namespace {

using geode::jni::FloatElements;
using geode::jni::Utf8Chars;

geode_stage* stageOf(jlong handle) { return reinterpret_cast<geode_stage*>(handle); }

// The stage's renderer for one any-thread geode_viz_* call; null once the stage let it go, which those calls ignore.
class StageViz {
public:
    explicit StageViz(jlong handle) : viz_(geode_stage_viz_lock(stageOf(handle))) {}
    ~StageViz() {
        if (viz_) geode_stage_viz_unlock();
    }
    StageViz(const StageViz&) = delete;
    StageViz& operator=(const StageViz&) = delete;
    operator geode_viz*() const { return viz_; }

private:
    geode_viz* viz_;
};

}  // namespace

extern "C" {

JNIEXPORT jlong JNICALL
Java_dev_geode_engine_bridge_GeodeNative_stageCreate(JNIEnv* env, jobject, jobject assetManager, jstring cacheDir,
                                                     jstring milkTextureDir, jint kind, jfloat capFps) {
    Utf8Chars cache(env, cacheDir);
    Utf8Chars textures(env, milkTextureDir);
    AAssetManager* assets = assetManager ? AAssetManager_fromJava(env, assetManager) : nullptr;
    return reinterpret_cast<jlong>(geode_stage_create_live(kind, assets, cache.get(), textures.get(), capFps));
}

JNIEXPORT void JNICALL
Java_dev_geode_engine_bridge_GeodeNative_stageDestroy(JNIEnv*, jobject, jlong stage) {
    geode_stage_destroy(stageOf(stage));
}

// The stage takes its own reference to the window, so this one is let go as soon as the call returns.
JNIEXPORT void JNICALL
Java_dev_geode_engine_bridge_GeodeNative_stageSetWindow(JNIEnv* env, jobject, jlong stage, jobject surface, jint width,
                                                        jint height) {
    ANativeWindow* window = surface ? ANativeWindow_fromSurface(env, surface) : nullptr;
    geode_stage_set_window(stageOf(stage), window, width, height);
    if (window) ANativeWindow_release(window);
}

JNIEXPORT void JNICALL
Java_dev_geode_engine_bridge_GeodeNative_stageSetSize(JNIEnv*, jobject, jlong stage, jint width, jint height) {
    geode_stage_set_size(stageOf(stage), width, height);
}

JNIEXPORT void JNICALL
Java_dev_geode_engine_bridge_GeodeNative_stageSetView(JNIEnv*, jobject, jlong stage, jint viewFlags) {
    geode_stage_set_view(stageOf(stage), static_cast<unsigned>(viewFlags));
}

JNIEXPORT jint JNICALL
Java_dev_geode_engine_bridge_GeodeNative_stageSetSurface(JNIEnv*, jobject, jlong stage, jint surfaceFlags,
                                                         jlong animateUntilMs, jlong nowMs) {
    return geode_stage_set_surface(stageOf(stage), static_cast<unsigned>(surfaceFlags), animateUntilMs, nowMs);
}

JNIEXPORT void JNICALL
Java_dev_geode_engine_bridge_GeodeNative_stagePause(JNIEnv*, jobject, jlong stage) {
    geode_stage_pause(stageOf(stage));
}

JNIEXPORT void JNICALL
Java_dev_geode_engine_bridge_GeodeNative_stageResume(JNIEnv*, jobject, jlong stage) {
    geode_stage_resume(stageOf(stage));
}

JNIEXPORT void JNICALL
Java_dev_geode_engine_bridge_GeodeNative_stageRedraw(JNIEnv*, jobject, jlong stage) {
    geode_stage_redraw(stageOf(stage));
}

JNIEXPORT void JNICALL
Java_dev_geode_engine_bridge_GeodeNative_stageSetCap(JNIEnv*, jobject, jlong stage, jfloat capFps) {
    geode_stage_set_cap(stageOf(stage), capFps);
}

JNIEXPORT jint JNICALL
Java_dev_geode_engine_bridge_GeodeNative_stageState(JNIEnv*, jobject, jlong stage) {
    return static_cast<jint>(geode_stage_state(stageOf(stage)));
}

JNIEXPORT jfloat JNICALL
Java_dev_geode_engine_bridge_GeodeNative_stagePacedFps(JNIEnv*, jobject, jlong stage) {
    return geode_stage_paced_fps(stageOf(stage));
}

JNIEXPORT jint JNICALL
Java_dev_geode_engine_bridge_GeodeNative_stageTakeEvents(JNIEnv*, jobject, jlong stage) {
    return static_cast<jint>(geode_stage_take_events(stageOf(stage)));
}

// A ring handle is valid while its Kotlin holder is reachable: the shell holds the ring until it has set another.
JNIEXPORT void JNICALL
Java_dev_geode_engine_bridge_GeodeNative_stageSetSound(JNIEnv*, jobject, jlong stage, jlong ring) {
    geode_stage_set_sound(stageOf(stage), reinterpret_cast<const geode_pcm_ring*>(ring));
}

JNIEXPORT void JNICALL
Java_dev_geode_engine_bridge_GeodeNative_stageSetLook(JNIEnv* env, jobject, jlong stage, jstring sceneId, jfloatArray params,
                                                      jfloatArray lfos, jint lfoSlots, jfloatArray adsrs, jfloatArray tuning,
                                                      jfloatArray reactions, jboolean reducedMotion, jstring transitionId,
                                                      jlong transitionMs) {
    Utf8Chars scene(env, sceneId);
    Utf8Chars transition(env, transitionId);
    FloatElements paramData(env, params);
    FloatElements lfoData(env, lfos);
    FloatElements adsrData(env, adsrs);
    FloatElements tuningData(env, tuning);
    FloatElements reactionData(env, reactions);
    if (reactions && !reactionData.get()) return;  // could not read the list: keep the last look
    geode_stage_look look{};
    look.scene_id = scene.get();
    look.params = paramData.get();
    look.param_count = paramData.length();
    look.lfos = lfoData.get();
    look.lfo_slots = lfoSlots;
    look.lfo_floats = lfoData.length();
    look.adsrs = adsrData.get();
    look.adsr_floats = adsrData.length();
    look.tuning = tuningData.get();
    look.tuning_count = tuningData.length();
    look.reactions = reactionData.get();
    look.reaction_count = reactionData.length();
    look.has_reactions = reactions ? 1 : 0;
    look.reduced_motion = reducedMotion == JNI_TRUE ? 1 : 0;
    look.transition_id = transition.get();
    look.transition_ms = transitionMs;
    geode_stage_set_look(stageOf(stage), &look);
}

// Checked against the array's length and treated as a clear when it does not fit, so a stale width or height
// never reads past the JVM's buffer.
JNIEXPORT void JNICALL
Java_dev_geode_engine_bridge_GeodeNative_stageSetUnderlay(JNIEnv* env, jobject, jlong stage, jintArray pixels, jint width,
                                                          jint height, jint blend, jfloat amount) {
    StageViz viz(stage);
    jint* data = pixels ? env->GetIntArrayElements(pixels, nullptr) : nullptr;
    if (pixels && !data) return;
    const jlong needed = static_cast<jlong>(width) * static_cast<jlong>(height);
    const bool fits = data && width > 0 && height > 0 && needed <= env->GetArrayLength(pixels);
    geode_viz_set_underlay_rgba(viz, fits ? reinterpret_cast<const uint32_t*>(data) : nullptr, fits ? width : 0, fits ? height : 0,
                                blend, amount);
    if (data) env->ReleaseIntArrayElements(pixels, data, JNI_ABORT);
}

JNIEXPORT void JNICALL
Java_dev_geode_engine_bridge_GeodeNative_stageSetQuality(JNIEnv*, jobject, jlong stage, jfloat renderScale, jfloat marchScale,
                                                         jboolean postFx, jboolean adaptive, jint liquidHotPolicy) {
    geode_viz_set_quality(StageViz(stage), renderScale, marchScale, postFx == JNI_TRUE ? 1 : 0, adaptive == JNI_TRUE ? 1 : 0,
                          liquidHotPolicy);
}

JNIEXPORT void JNICALL
Java_dev_geode_engine_bridge_GeodeNative_stageBeginParamMorph(JNIEnv*, jobject, jlong stage, jfloat seconds) {
    geode_viz_begin_param_morph(StageViz(stage), seconds);
}

JNIEXPORT void JNICALL
Java_dev_geode_engine_bridge_GeodeNative_stageSetTouch(JNIEnv* env, jobject, jlong stage, jfloatArray xy, jint points) {
    FloatElements data(env, xy);
    const jint available = data.get() ? data.length() / 2 : 0;
    geode_viz_set_touch(StageViz(stage), data.get(), std::min(points, available));
}

JNIEXPORT void JNICALL
Java_dev_geode_engine_bridge_GeodeNative_stageSetTouchSmear(JNIEnv*, jobject, jlong stage, jboolean on, jfloat strength) {
    geode_viz_set_touch_smear(StageViz(stage), on == JNI_TRUE ? 1 : 0, strength);
}

JNIEXPORT void JNICALL
Java_dev_geode_engine_bridge_GeodeNative_stageSetFluidInjection(JNIEnv* env, jobject, jlong stage, jstring force, jstring dye) {
    Utf8Chars forceSrc(env, force);
    Utf8Chars dyeSrc(env, dye);
    geode_viz_set_fluid_injection(StageViz(stage), forceSrc.get(), dyeSrc.get());
}

JNIEXPORT void JNICALL
Java_dev_geode_engine_bridge_GeodeNative_stageLoadMilkPreset(JNIEnv* env, jobject, jlong stage, jstring path) {
    Utf8Chars file(env, path);
    geode_viz_load_milk_preset(StageViz(stage), file.get());
}

JNIEXPORT void JNICALL
Java_dev_geode_engine_bridge_GeodeNative_stageReloadMilkPreset(JNIEnv*, jobject, jlong stage) {
    geode_viz_reload_milk_preset(StageViz(stage));
}

// One call: the take hands the path out once. A path longer than PATH_MAX is no file the renderer loaded.
JNIEXPORT jstring JNICALL
Java_dev_geode_engine_bridge_GeodeNative_stageTakeMilkLoaded(JNIEnv* env, jobject, jlong stage) {
    char path[4096];
    const size_t length = geode_stage_take_milk_loaded(stageOf(stage), path, sizeof(path));
    return length > 0 && length < sizeof(path) ? geode::jni::utf8String(env, path) : nullptr;
}

JNIEXPORT jint JNICALL
Java_dev_geode_engine_bridge_GeodeNative_stageTakeDirectorMoments(JNIEnv*, jobject, jlong stage) {
    return geode_stage_take_director_moments(stageOf(stage));
}

JNIEXPORT jint JNICALL
Java_dev_geode_engine_bridge_GeodeNative_stageLiquidMode(JNIEnv*, jobject, jlong stage) {
    return geode_stage_liquid_mode(stageOf(stage));
}

JNIEXPORT jstring JNICALL
Java_dev_geode_engine_bridge_GeodeNative_stageError(JNIEnv* env, jobject, jlong stage) {
    std::vector<char> text(geode_stage_error(stageOf(stage), nullptr, 0) + 1, '\0');
    geode_stage_error(stageOf(stage), text.data(), text.size());
    return geode::jni::utf8String(env, text.data());
}

JNIEXPORT void JNICALL
Java_dev_geode_engine_bridge_GeodeNative_stageGyroPush(JNIEnv*, jobject, jlong stage, jfloat x, jfloat y, jfloat z, jfloat w,
                                                       jint displayRotation) {
    geode_viz_gyro_push(StageViz(stage), x, y, z, w, displayRotation);
}

JNIEXPORT jboolean JNICALL
Java_dev_geode_engine_bridge_GeodeNative_stageGyroListens(JNIEnv*, jobject, jlong stage, jboolean wanted) {
    return geode_stage_gyro_listens(stageOf(stage), wanted == JNI_TRUE ? 1 : 0) ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT void JNICALL
Java_dev_geode_engine_bridge_GeodeNative_stageSetUiBlurRegion(JNIEnv*, jobject, jlong stage, jfloat left, jfloat top,
                                                              jfloat right, jfloat bottom, jfloat strength, jfloat cornerPx) {
    geode_viz_set_ui_blur_region(StageViz(stage), left, top, right, bottom, strength, cornerPx);
}

JNIEXPORT void JNICALL
Java_dev_geode_engine_bridge_GeodeNative_stageSetEdgeGlow(JNIEnv* env, jobject, jlong stage, jfloatArray opts) {
    const jsize count = opts ? std::min<jsize>(env->GetArrayLength(opts), GEODE_EDGE_GLOW_FLOATS) : 0;
    float values[GEODE_EDGE_GLOW_FLOATS] = {};
    if (count > 0) env->GetFloatArrayRegion(opts, 0, count, reinterpret_cast<jfloat*>(values));
    geode_viz_set_edge_glow(StageViz(stage), opts ? values : nullptr, static_cast<int>(count));
}

JNIEXPORT void JNICALL
Java_dev_geode_engine_bridge_GeodeNative_stageSetCastSource(JNIEnv*, jobject, jlong stage, jboolean on) {
    geode_viz_set_cast_source(StageViz(stage), on == JNI_TRUE ? 1 : 0);
}

JNIEXPORT jboolean JNICALL
Java_dev_geode_engine_bridge_GeodeNative_stageKnows(JNIEnv* env, jobject, jlong stage, jstring sceneId) {
    Utf8Chars id(env, sceneId);
    return geode_viz_knows(StageViz(stage), id.get()) ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jstring JNICALL
Java_dev_geode_engine_bridge_GeodeNative_stageSceneIds(JNIEnv* env, jobject, jlong stage) {
    StageViz viz(stage);
    std::vector<char> ids(geode_viz_scene_ids(viz, nullptr, 0) + 1, '\0');
    geode_viz_scene_ids(viz, ids.data(), ids.size());
    return env->NewStringUTF(ids.data());
}

JNIEXPORT void JNICALL
Java_dev_geode_engine_bridge_GeodeNative_stageWarmTransition(JNIEnv* env, jobject, jlong stage, jstring id) {
    Utf8Chars name(env, id);
    geode_stage_warm_transition(stageOf(stage), name.get());
}

// The tile is copied by the call, so the buffer is the caller's again when it returns. A heap buffer has no
// address: it crosses as no tile, which a recording that owes a mark fails on.
JNIEXPORT jboolean JNICALL
Java_dev_geode_engine_bridge_GeodeNative_stageRecordStart(JNIEnv* env, jobject, jlong stage, jlong recorder, jlong session,
                                                          jobject tile, jint tileWidth, jint tileHeight, jint placement,
                                                          jint corner) {
    const jlong needed = static_cast<jlong>(tileWidth) * static_cast<jlong>(tileHeight) * 4;
    auto* bytes = tile ? static_cast<const uint8_t*>(env->GetDirectBufferAddress(tile)) : nullptr;
    const bool fits = bytes && needed > 0 && env->GetDirectBufferCapacity(tile) >= needed;
    return geode_stage_record_start(stageOf(stage), reinterpret_cast<geode_recorder*>(recorder),
                                    reinterpret_cast<geode_record_session*>(session), fits ? bytes : nullptr,
                                    fits ? tileWidth : 0, fits ? tileHeight : 0, placement, corner)
               ? JNI_TRUE
               : JNI_FALSE;
}

JNIEXPORT void JNICALL
Java_dev_geode_engine_bridge_GeodeNative_stageRecordStop(JNIEnv*, jobject, jlong stage) {
    geode_stage_record_stop(stageOf(stage));
}

JNIEXPORT jboolean JNICALL
Java_dev_geode_engine_bridge_GeodeNative_stageRecordClosed(JNIEnv*, jobject, jlong stage) {
    return geode_stage_record_closed(stageOf(stage)) ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT void JNICALL
Java_dev_geode_engine_bridge_GeodeNative_stageStillRequest(JNIEnv*, jobject, jlong stage) {
    geode_viz_still_request(StageViz(stage));
}

JNIEXPORT jboolean JNICALL
Java_dev_geode_engine_bridge_GeodeNative_stageStillSize(JNIEnv* env, jobject, jlong stage, jintArray out) {
    if (!out || env->GetArrayLength(out) < 2) return JNI_FALSE;
    jint size[2] = {0, 0};
    if (!geode_stage_still_size(stageOf(stage), &size[0], &size[1])) return JNI_FALSE;
    env->SetIntArrayRegion(out, 0, 2, size);
    return JNI_TRUE;
}

JNIEXPORT jboolean JNICALL
Java_dev_geode_engine_bridge_GeodeNative_stageStillRead(JNIEnv* env, jobject, jlong stage, jobject out) {
    void* bytes = out ? env->GetDirectBufferAddress(out) : nullptr;
    const jlong capacity = bytes ? env->GetDirectBufferCapacity(out) : 0;
    return bytes && capacity > 0 && geode_stage_still_read(stageOf(stage), bytes, static_cast<size_t>(capacity)) ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jboolean JNICALL
Java_dev_geode_engine_bridge_GeodeNative_stageTapAlong(JNIEnv*, jobject, jlong stage, jlong uptimeNs) {
    return geode_viz_tap_along(StageViz(stage), uptimeNs) ? JNI_TRUE : JNI_FALSE;
}

}  // extern "C"
