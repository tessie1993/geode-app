#include <jni.h>
#include <android/asset_manager_jni.h>

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

#include "api/geode_api.h"
#include "jni_util.hpp"

namespace {

using geode::jni::FloatElements;
using geode::jni::Utf8Chars;

geode_viz* vizOf(jlong handle) { return reinterpret_cast<geode_viz*>(handle); }

// A Java String[] as owned UTF-8 strings (a null element becomes "").
std::vector<std::string> utf8Strings(JNIEnv* env, jobjectArray array) {
    std::vector<std::string> out;
    const jsize n = array ? env->GetArrayLength(array) : 0;
    out.reserve(static_cast<size_t>(n));
    for (jsize i = 0; i < n; ++i) {
        auto str = static_cast<jstring>(env->GetObjectArrayElement(array, i));
        {
            Utf8Chars chars(env, str);
            out.emplace_back(chars.get() ? chars.get() : "");
        }
        if (str) env->DeleteLocalRef(str);
    }
    return out;
}

std::vector<const char*> cStrings(const std::vector<std::string>& strings) {
    std::vector<const char*> out;
    out.reserve(strings.size());
    for (const auto& s : strings) out.push_back(s.c_str());
    return out;
}

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

JNIEXPORT void JNICALL
Java_dev_geode_engine_bridge_GeodeNative_vizSetReducedMotion(JNIEnv*, jobject, jlong handle, jboolean on) {
    geode_viz_set_reduced_motion(vizOf(handle), on == JNI_TRUE);
}

JNIEXPORT void JNICALL
Java_dev_geode_engine_bridge_GeodeNative_layerSet(JNIEnv* env, jobject, jstring sceneId, jfloat mix, jint blendMode) {
    Utf8Chars id(env, sceneId);
    geode_layer_set(id.get() ? id.get() : "", mix, blendMode);
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
Java_dev_geode_engine_bridge_GeodeNative_vizQueueTouchStroke(JNIEnv*, jobject, jlong handle, jfloat nx, jfloat ny, jfloat ndx, jfloat ndy,
                                                             jfloat dt, jfloat strength) {
    geode_viz_queue_touch_stroke(vizOf(handle), nx, ny, ndx, ndy, dt, strength);
}

JNIEXPORT void JNICALL
Java_dev_geode_engine_bridge_GeodeNative_vizSetFluidInjection(JNIEnv* env, jobject, jlong handle, jstring force, jstring dye) {
    Utf8Chars forceSrc(env, force);
    Utf8Chars dyeSrc(env, dye);
    geode_viz_set_fluid_injection(vizOf(handle), forceSrc.get(), dyeSrc.get());
}

JNIEXPORT void JNICALL
Java_dev_geode_engine_bridge_GeodeNative_vizLoadMilkPreset(JNIEnv* env, jobject, jlong handle, jstring path) {
    Utf8Chars file(env, path);
    geode_viz_load_milk_preset(vizOf(handle), file.get());
}

JNIEXPORT void JNICALL
Java_dev_geode_engine_bridge_GeodeNative_vizReloadMilkPreset(JNIEnv*, jobject, jlong handle) {
    geode_viz_reload_milk_preset(vizOf(handle));
}

JNIEXPORT void JNICALL
Java_dev_geode_engine_bridge_GeodeNative_vizSetMilkTextureDir(JNIEnv* env, jobject, jlong handle, jstring dir) {
    Utf8Chars path(env, dir);
    geode_viz_set_milk_texture_dir(vizOf(handle), path.get());
}

JNIEXPORT jstring JNICALL
Java_dev_geode_engine_bridge_GeodeNative_vizTakeMilkPresetLoaded(JNIEnv* env, jobject, jlong handle) {
    // One call: the take hands the path out once, so a length query first would leave the copy
    // empty. A path longer than PATH_MAX cannot be a file the renderer loaded.
    char buffer[4096];
    const size_t length = geode_viz_take_milk_preset_loaded(vizOf(handle), buffer, sizeof(buffer));
    if (length == 0 || length >= sizeof(buffer)) return nullptr;
    return env->NewStringUTF(buffer);
}

JNIEXPORT jint JNICALL
Java_dev_geode_engine_bridge_GeodeNative_vizTakeDirectorStyleMoments(JNIEnv*, jobject, jlong handle) {
    return geode_viz_take_director_style_moments(vizOf(handle));
}

JNIEXPORT void JNICALL
Java_dev_geode_engine_bridge_GeodeNative_vizPushPcm(JNIEnv* env, jobject, jlong handle, jfloatArray mono, jint count) {
    FloatElements data(env, mono);
    if (!data.get()) return;
    geode_viz_push_pcm(vizOf(handle), data.get(), count < data.length() ? count : data.length());
}

// Region copy: every rendered frame, a few hundred frames of audio; never pins the whole scratch array.
JNIEXPORT void JNICALL
Java_dev_geode_engine_bridge_GeodeNative_vizWriteAudio(JNIEnv* env, jobject, jlong handle, jfloatArray interleaved,
                                                       jint frames, jint channels, jint sampleRate) {
    if (!interleaved || frames <= 0 || channels <= 0) return;
    const jint available = env->GetArrayLength(interleaved) / channels;
    const jint count = frames < available ? frames : available;
    std::vector<float> data(static_cast<size_t>(count) * static_cast<size_t>(channels));
    env->GetFloatArrayRegion(interleaved, 0, static_cast<jsize>(data.size()), data.data());
    geode_viz_write_audio(vizOf(handle), data.data(), count, channels, sampleRate);
}

JNIEXPORT void JNICALL
Java_dev_geode_engine_bridge_GeodeNative_vizResetAudio(JNIEnv*, jobject, jlong handle) {
    geode_viz_reset_audio(vizOf(handle));
}

JNIEXPORT void JNICALL
Java_dev_geode_engine_bridge_GeodeNative_vizSetSoundTuning(JNIEnv* env, jobject, jlong handle, jfloatArray tuning) {
    if (!tuning || env->GetArrayLength(tuning) < GEODE_SOUND_TUNING_FLOATS) return;
    GeodeSoundTuning t{};
    env->GetFloatArrayRegion(tuning, 0, GEODE_SOUND_TUNING_FLOATS, reinterpret_cast<jfloat*>(&t));
    geode_viz_set_sound_tuning(vizOf(handle), &t);
}

JNIEXPORT jstring JNICALL
Java_dev_geode_engine_bridge_GeodeNative_reactSourceCatalog(JNIEnv* env, jobject) {
    const int need = geode_react_source_catalog(nullptr, 0);
    std::vector<char> text(static_cast<size_t>(need) + 1);
    geode_react_source_catalog(text.data(), need + 1);
    return env->NewStringUTF(text.data());
}

JNIEXPORT void JNICALL
Java_dev_geode_engine_bridge_GeodeNative_macrosSet(JNIEnv* env, jobject, jfloatArray knobs) {
    if (!knobs) return;
    const jsize n = std::min<jsize>(env->GetArrayLength(knobs), 16);
    float k[16]{};
    env->GetFloatArrayRegion(knobs, 0, n, k);
    geode_macros_set(k, n);
}

JNIEXPORT void JNICALL
Java_dev_geode_engine_bridge_GeodeNative_directorSet(JNIEnv*, jobject, jfloat amount, jlong seed) {
    geode_director_set(amount, static_cast<long long>(seed));
}

JNIEXPORT void JNICALL
Java_dev_geode_engine_bridge_GeodeNative_noteResponseSet(JNIEnv*, jobject, jboolean enabled) {
    geode_note_response_set(enabled ? 1 : 0);
}

JNIEXPORT jfloatArray JNICALL
Java_dev_geode_engine_bridge_GeodeNative_macrosSurprise(JNIEnv* env, jobject, jfloatArray knobs, jint lockMask, jlong seed) {
    const jsize n = knobs ? std::min<jsize>(env->GetArrayLength(knobs), 16) : 0;
    float k[16]{};
    float out[16]{};
    if (n > 0) env->GetFloatArrayRegion(knobs, 0, n, k);
    geode_macros_surprise(k, n, static_cast<unsigned>(lockMask), static_cast<long long>(seed), out);
    jfloatArray result = env->NewFloatArray(n);
    if (result) env->SetFloatArrayRegion(result, 0, n, out);
    return result;
}

JNIEXPORT jstring JNICALL
Java_dev_geode_engine_bridge_GeodeNative_macroStarters(JNIEnv* env, jobject) {
    const int need = geode_macro_starters(nullptr, 0);
    std::vector<char> text(static_cast<size_t>(need) + 1);
    geode_macro_starters(text.data(), need + 1);
    return env->NewStringUTF(text.data());
}

JNIEXPORT void JNICALL
Java_dev_geode_engine_bridge_GeodeNative_shaderOverrideSet(JNIEnv* env, jobject, jstring styleId,
                                                           jstring fragmentSource) {
    Utf8Chars id(env, styleId);
    Utf8Chars source(env, fragmentSource);
    geode_shader_override_set(id.get(), source.get());
}

JNIEXPORT jstring JNICALL
Java_dev_geode_engine_bridge_GeodeNative_shaderOverride(JNIEnv* env, jobject, jstring styleId) {
    Utf8Chars id(env, styleId);
    const size_t length = geode_shader_override(id.get(), nullptr, 0);
    if (length == 0) return nullptr;
    std::vector<char> buffer(length + 1, '\0');
    geode_shader_override(id.get(), buffer.data(), buffer.size());
    return env->NewStringUTF(buffer.data());
}

JNIEXPORT void JNICALL
Java_dev_geode_engine_bridge_GeodeNative_vizSetLfo(JNIEnv* env, jobject, jlong handle, jint slot, jfloatArray config) {
    FloatElements data(env, config);
    if (data.get()) geode_viz_set_lfo(vizOf(handle), slot, data.get(), data.length());
}

JNIEXPORT void JNICALL
Java_dev_geode_engine_bridge_GeodeNative_vizSetReactions(JNIEnv* env, jobject, jlong handle, jfloatArray routes) {
    FloatElements data(env, routes);
    if (data.get()) geode_viz_set_reactions(vizOf(handle), data.get(), data.length());
}

JNIEXPORT jfloatArray JNICALL
Java_dev_geode_engine_bridge_GeodeNative_styleReactions(JNIEnv* env, jobject, jstring styleId, jint seed) {
    Utf8Chars id(env, styleId);
    float routes[GEODE_REACT_ROUTES * GEODE_REACT_ROUTE_FLOATS] = {};
    const int n = id.get() ? geode_style_reactions(id.get(), seed, routes, GEODE_REACT_ROUTES * GEODE_REACT_ROUTE_FLOATS) : 0;
    jfloatArray out = env->NewFloatArray(n * GEODE_REACT_ROUTE_FLOATS);
    if (out && n > 0) env->SetFloatArrayRegion(out, 0, n * GEODE_REACT_ROUTE_FLOATS, routes);
    return out;
}

JNIEXPORT void JNICALL
Java_dev_geode_engine_bridge_GeodeNative_handSet(JNIEnv*, jobject, jint mask, jboolean hasUser) {
    geode_hand_set(static_cast<unsigned>(mask), hasUser ? 1 : 0);
}

JNIEXPORT jfloatArray JNICALL
Java_dev_geode_engine_bridge_GeodeNative_styleReactionsHand(JNIEnv* env, jobject, jstring styleId, jint seed, jfloatArray user, jint mask) {
    Utf8Chars id(env, styleId);
    FloatElements routes(env, user);
    float out[GEODE_REACT_ROUTES * GEODE_REACT_ROUTE_FLOATS] = {};
    const int n = id.get() ? geode_style_reactions_hand(id.get(), seed, routes.get(), routes.length(), user ? 1 : 0,
                                                        static_cast<unsigned>(mask), out, GEODE_REACT_ROUTES * GEODE_REACT_ROUTE_FLOATS)
                           : 0;
    jfloatArray result = env->NewFloatArray(n * GEODE_REACT_ROUTE_FLOATS);
    if (result && n > 0) env->SetFloatArrayRegion(result, 0, n * GEODE_REACT_ROUTE_FLOATS, out);
    return result;
}

JNIEXPORT jfloatArray JNICALL
Java_dev_geode_engine_bridge_GeodeNative_musicState(JNIEnv* env, jobject, jstring styleId, jint seed, jfloatArray user, jint mask) {
    Utf8Chars id(env, styleId);
    FloatElements routes(env, user);
    float out[GEODE_MUSIC_MOMENTS * GEODE_MUSIC_STATE_FLOATS] = {};
    const int n = id.get() ? geode_music_state(id.get(), seed, routes.get(), routes.length(), user ? 1 : 0,
                                               static_cast<unsigned>(mask), out, GEODE_MUSIC_MOMENTS * GEODE_MUSIC_STATE_FLOATS)
                           : 0;
    jfloatArray result = env->NewFloatArray(n * GEODE_MUSIC_STATE_FLOATS);
    if (result && n > 0) env->SetFloatArrayRegion(result, 0, n * GEODE_MUSIC_STATE_FLOATS, out);
    return result;
}

// outHand[0] is the new mask, outHand[1] 1 while the user has a list (0: all Auto); returns the user's routes.
JNIEXPORT jfloatArray JNICALL
Java_dev_geode_engine_bridge_GeodeNative_musicEdit(JNIEnv* env, jobject, jstring styleId, jint seed, jfloatArray user, jint mask, jint op,
                                                   jint moment, jint action, jfloat value, jintArray outHand) {
    Utf8Chars id(env, styleId);
    FloatElements routes(env, user);
    float out[GEODE_REACT_ROUTES * GEODE_REACT_ROUTE_FLOATS] = {};
    unsigned nextMask = 0;
    int hasUser = 0;
    // A null id leaves the user's side as it was (geode_music_edit's own rule), never "all Auto" by accident.
    const int n = geode_music_edit(id.get(), seed, routes.get(), routes.length(), user ? 1 : 0, static_cast<unsigned>(mask), op,
                                   moment, action, value, out, GEODE_REACT_ROUTES * GEODE_REACT_ROUTE_FLOATS, &nextMask, &hasUser);
    if (outHand && env->GetArrayLength(outHand) >= 2) {
        const jint hand[2] = {static_cast<jint>(nextMask), hasUser};
        env->SetIntArrayRegion(outHand, 0, 2, hand);
    }
    jfloatArray result = env->NewFloatArray(n * GEODE_REACT_ROUTE_FLOATS);
    if (result && n > 0) env->SetFloatArrayRegion(result, 0, n * GEODE_REACT_ROUTE_FLOATS, out);
    return result;
}

JNIEXPORT jint JNICALL
Java_dev_geode_engine_bridge_GeodeNative_musicActionTarget(JNIEnv*, jobject, jint action) {
    return geode_music_action_target(action);
}

JNIEXPORT void JNICALL
Java_dev_geode_engine_bridge_GeodeNative_vizSetLook(JNIEnv*, jobject, jlong handle, jfloat yaw, jfloat pitch) {
    geode_viz_set_look(vizOf(handle), yaw, pitch);
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
Java_dev_geode_engine_bridge_GeodeNative_vizSetQuality(JNIEnv*, jobject, jlong handle, jfloat renderScale,
                                                       jfloat marchScale, jboolean postFx, jboolean adaptive) {
    geode_viz_set_quality(vizOf(handle), renderScale, marchScale, postFx == JNI_TRUE, adaptive == JNI_TRUE);
}

JNIEXPORT void JNICALL
Java_dev_geode_engine_bridge_GeodeNative_vizSetLiquidHotPolicy(JNIEnv*, jobject, jlong handle, jint policy) {
    geode_viz_set_liquid_hot_policy(vizOf(handle), policy);
}

JNIEXPORT jint JNICALL
Java_dev_geode_engine_bridge_GeodeNative_vizLiquidMode(JNIEnv*, jobject, jlong handle) {
    return geode_viz_liquid_mode(vizOf(handle));
}

JNIEXPORT void JNICALL
Java_dev_geode_engine_bridge_GeodeNative_vizSetDirector(JNIEnv*, jobject, jlong handle, jfloat amount, jlong seed) {
    geode_viz_set_director(vizOf(handle), amount, static_cast<long long>(seed));
}

JNIEXPORT void JNICALL
Java_dev_geode_engine_bridge_GeodeNative_vizSetOffscreen(JNIEnv*, jobject, jlong handle, jboolean on) {
    geode_viz_set_offscreen(vizOf(handle), on == JNI_TRUE);
}

JNIEXPORT void JNICALL
Java_dev_geode_engine_bridge_GeodeNative_vizSetFrameRate(JNIEnv*, jobject, jlong handle, jint fps) {
    geode_viz_set_frame_rate(vizOf(handle), fps);
}

JNIEXPORT void JNICALL
Java_dev_geode_engine_bridge_GeodeNative_vizSetUiBlurRegion(JNIEnv*, jobject, jlong handle, jfloat left, jfloat top,
                                                            jfloat right, jfloat bottom, jfloat strength, jfloat cornerPx) {
    geode_viz_set_ui_blur_region(vizOf(handle), left, top, right, bottom, strength, cornerPx);
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
Java_dev_geode_engine_bridge_GeodeNative_vizSetStyleSchedule(JNIEnv* env, jobject, jlong handle, jdoubleArray atMs,
                                                             jobjectArray sceneIds, jobjectArray milkPaths,
                                                             jobjectArray cutIds, jint everyBeats,
                                                             jobjectArray lockedIds) {
    const std::vector<std::string> scenes = utf8Strings(env, sceneIds);
    const std::vector<std::string> milks = utf8Strings(env, milkPaths);
    const std::vector<std::string> cut = utf8Strings(env, cutIds);
    const size_t timed = atMs ? static_cast<size_t>(env->GetArrayLength(atMs)) : 0;
    const size_t count = std::min(timed, std::min(scenes.size(), milks.size()));
    std::vector<double> times(count);
    if (count > 0) env->GetDoubleArrayRegion(atMs, 0, static_cast<jsize>(count), times.data());
    const std::vector<const char*> sceneChars = cStrings(scenes);
    const std::vector<const char*> milkChars = cStrings(milks);
    const std::vector<const char*> cutChars = cStrings(cut);
    const std::vector<std::string> locked = utf8Strings(env, lockedIds);
    const std::vector<const char*> lockedChars = cStrings(locked);
    geode_viz_set_style_schedule(vizOf(handle), times.data(), sceneChars.data(), milkChars.data(),
                                 static_cast<int>(count), cutChars.data(), static_cast<int>(cutChars.size()), everyBeats,
                                 lockedChars.data(), static_cast<int>(lockedChars.size()));
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
Java_dev_geode_engine_bridge_GeodeNative_vizPrepareOffscreen(JNIEnv*, jobject, jlong handle) {
    geode_viz_prepare_offscreen(vizOf(handle));
}

JNIEXPORT void JNICALL
Java_dev_geode_engine_bridge_GeodeNative_vizReleaseScenes(JNIEnv*, jobject, jlong handle) {
    geode_viz_release_scenes(vizOf(handle));
}

// pixels is a Java IntArray? (ARGB per Android's Bitmap.getPixels), so a null array is a valid "clear"
// call rather than a JNI error - handled before touching the (possibly null) array. width*height is
// checked against the array's actual length (mirroring vizPushPcm's count clamp above) and treated as a
// clear when it doesn't fit, so a stale/mismatched width or height can never make
// geode_viz_set_underlay_rgba read past the JVM-owned buffer.
JNIEXPORT void JNICALL
Java_dev_geode_engine_bridge_GeodeNative_vizSetUnderlay(JNIEnv* env, jobject, jlong handle, jintArray pixels, jint width, jint height,
                                                        jint blend, jfloat amount) {
    if (!pixels) {
        geode_viz_set_underlay_rgba(vizOf(handle), nullptr, 0, 0, blend, amount);
        return;
    }
    jint* data = env->GetIntArrayElements(pixels, nullptr);
    if (!data) return;
    const jlong needed = static_cast<jlong>(width) * static_cast<jlong>(height);
    if (width > 0 && height > 0 && needed <= env->GetArrayLength(pixels)) {
        geode_viz_set_underlay_rgba(vizOf(handle), reinterpret_cast<const uint32_t*>(data), width, height, blend, amount);
    } else {
        geode_viz_set_underlay_rgba(vizOf(handle), nullptr, 0, 0, blend, amount);
    }
    env->ReleaseIntArrayElements(pixels, data, JNI_ABORT);
}

JNIEXPORT jstring JNICALL
Java_dev_geode_engine_bridge_GeodeNative_catalogueText(JNIEnv* env, jobject) {
    return env->NewStringUTF(geode_catalogue_text());
}

JNIEXPORT jstring JNICALL
Java_dev_geode_engine_bridge_GeodeNative_customizeText(JNIEnv* env, jobject) {
    return env->NewStringUTF(geode_customize_text());
}

JNIEXPORT jboolean JNICALL
Java_dev_geode_engine_bridge_GeodeNative_customizeLockable(JNIEnv* env, jobject, jstring key) {
    Utf8Chars name(env, key);
    return geode_customize_lockable(name.get()) ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jstring JNICALL
Java_dev_geode_engine_bridge_GeodeNative_customizeShown(JNIEnv* env, jobject, jstring styleId) {
    Utf8Chars style(env, styleId);
    return env->NewStringUTF(geode_customize_shown(style.get()));
}

JNIEXPORT jboolean JNICALL
Java_dev_geode_engine_bridge_GeodeNative_customizeRandomize(JNIEnv* env, jobject, jstring styleId, jstring category, jstring locked,
                                                            jlong seed, jfloatArray in, jfloatArray out) {
    if (!in || !out || env->GetArrayLength(in) < geode_viz_param_count() || env->GetArrayLength(out) < geode_viz_param_count()) {
        return JNI_FALSE;
    }
    Utf8Chars style(env, styleId);
    Utf8Chars categoryName(env, category);
    Utf8Chars lockedKeys(env, locked);
    std::vector<float> input(static_cast<size_t>(geode_viz_param_count()));
    std::vector<float> output(input.size());
    env->GetFloatArrayRegion(in, 0, static_cast<jsize>(input.size()), input.data());
    const int changed = geode_customize_randomize(style.get(), categoryName.get(), lockedKeys.get(), static_cast<uint64_t>(seed),
                                                  input.data(), output.data());
    env->SetFloatArrayRegion(out, 0, static_cast<jsize>(output.size()), output.data());
    return changed ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT void JNICALL
Java_dev_geode_engine_bridge_GeodeNative_customizeReset(JNIEnv* env, jobject, jstring category, jfloatArray in, jfloatArray out) {
    if (!in || !out || env->GetArrayLength(in) < geode_viz_param_count() || env->GetArrayLength(out) < geode_viz_param_count()) return;
    Utf8Chars categoryName(env, category);
    std::vector<float> input(static_cast<size_t>(geode_viz_param_count()));
    std::vector<float> output(input.size());
    env->GetFloatArrayRegion(in, 0, static_cast<jsize>(input.size()), input.data());
    geode_customize_reset(categoryName.get(), input.data(), output.data());
    env->SetFloatArrayRegion(out, 0, static_cast<jsize>(output.size()), output.data());
}

JNIEXPORT jboolean JNICALL
Java_dev_geode_engine_bridge_GeodeNative_paramsRandomize(JNIEnv* env, jobject, jstring styleId, jstring tab, jstring locked,
                                                         jlong seed, jfloatArray in, jfloatArray out) {
    if (!in || !out || env->GetArrayLength(in) < geode_viz_param_count() || env->GetArrayLength(out) < geode_viz_param_count()) {
        return JNI_FALSE;
    }
    Utf8Chars style(env, styleId);
    Utf8Chars tabName(env, tab);
    Utf8Chars lockedKeys(env, locked);
    std::vector<float> input(static_cast<size_t>(geode_viz_param_count()));
    std::vector<float> output(input.size());
    env->GetFloatArrayRegion(in, 0, static_cast<jsize>(input.size()), input.data());
    const int changed = geode_params_randomize(style.get(), tabName.get(), lockedKeys.get(), static_cast<uint64_t>(seed),
                                               input.data(), output.data());
    env->SetFloatArrayRegion(out, 0, static_cast<jsize>(output.size()), output.data());
    return changed ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jbyteArray JNICALL
Java_dev_geode_engine_bridge_GeodeNative_transitionsTable(JNIEnv* env, jobject, jobject assetManager) {
    // Bytes, not a jstring: NewStringUTF takes modified UTF-8, and the library's text is plain UTF-8.
    AAssetManager* assets = assetManager ? AAssetManager_fromJava(env, assetManager) : nullptr;
    const char* table = geode_transitions_table(assets);
    const auto n = static_cast<jsize>(std::strlen(table));
    jbyteArray out = env->NewByteArray(n);
    if (out) env->SetByteArrayRegion(out, 0, n, reinterpret_cast<const jbyte*>(table));
    return out;
}

JNIEXPORT jboolean JNICALL
Java_dev_geode_engine_bridge_GeodeNative_perfTier(JNIEnv* env, jobject, jint mode, jfloatArray out) {
    if (!out || env->GetArrayLength(out) < GEODE_PERF_TIER_FLOATS) return JNI_FALSE;
    float values[GEODE_PERF_TIER_FLOATS];
    if (geode_perf_tier(mode, values, GEODE_PERF_TIER_FLOATS) != GEODE_PERF_TIER_FLOATS) return JNI_FALSE;
    env->SetFloatArrayRegion(out, 0, GEODE_PERF_TIER_FLOATS, values);
    return JNI_TRUE;
}

JNIEXPORT jint JNICALL
Java_dev_geode_engine_bridge_GeodeNative_perfModeForDevice(JNIEnv*, jobject, jlong totalRamBytes, jboolean lowRam,
                                                          jint mediaPerformanceClass) {
    return geode_perf_mode_for_device(totalRamBytes, lowRam ? 1 : 0, mediaPerformanceClass);
}

JNIEXPORT jdouble JNICALL
Java_dev_geode_engine_bridge_GeodeNative_pacerOnVsync(JNIEnv* env, jobject, jlongArray state, jlong frameTimeNanos,
                                                     jfloat capFps) {
    if (!state || env->GetArrayLength(state) < GEODE_PACER_STATE_LONGS) return 0.0;
    long long wire[GEODE_PACER_STATE_LONGS];
    env->GetLongArrayRegion(state, 0, GEODE_PACER_STATE_LONGS, reinterpret_cast<jlong*>(wire));
    const double dt = geode_pacer_on_vsync(wire, frameTimeNanos, capFps);
    env->SetLongArrayRegion(state, 0, GEODE_PACER_STATE_LONGS, reinterpret_cast<const jlong*>(wire));
    return dt;
}

JNIEXPORT void JNICALL
Java_dev_geode_engine_bridge_GeodeNative_pacerResume(JNIEnv* env, jobject, jlongArray state) {
    if (!state || env->GetArrayLength(state) < GEODE_PACER_STATE_LONGS) return;
    long long wire[GEODE_PACER_STATE_LONGS];
    env->GetLongArrayRegion(state, 0, GEODE_PACER_STATE_LONGS, reinterpret_cast<jlong*>(wire));
    geode_pacer_resume(wire);
    env->SetLongArrayRegion(state, 0, GEODE_PACER_STATE_LONGS, reinterpret_cast<const jlong*>(wire));
}

}
