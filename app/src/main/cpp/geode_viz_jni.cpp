#include <jni.h>
#include <android/asset_manager_jni.h>

#include <algorithm>
#include <cstring>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "api/geode_api.h"
#include "api/geode_modulation_api.h"
#include "jni_util.hpp"

namespace {

// A palette swatch's colours as a Java int[]; at most kMaxSwatch, which no swatch needs more of.
jintArray toJava(JNIEnv* env, const std::vector<int>& values) {
    jintArray out = env->NewIntArray(static_cast<jsize>(values.size()));
    if (out) env->SetIntArrayRegion(out, 0, static_cast<jsize>(values.size()), reinterpret_cast<const jint*>(values.data()));
    return out;
}
constexpr jint kMaxSwatch = 256;


using geode::jni::FloatElements;
using geode::jni::Utf8Chars;

// Liveness of every renderer handed to Kotlin (#853). Kotlin checks `handle != 0L` and then calls,
// and destroy() on another thread can run between the two; a raw pointer would then be freed
// memory. Each handle has a reader/writer lock: every call holds it shared for the whole native
// call, destroy takes it exclusive after unregistering, so it waits for calls in flight and a
// later (stale) handle finds nothing and becomes a no-op.
struct VizEntry {
    std::shared_mutex lock;
};

std::mutex gVizMutex;
std::unordered_map<geode_viz*, std::shared_ptr<VizEntry>> gVizLive;

// A live renderer held for the length of the full expression it appears in; null for a stale handle.
class VizRef {
public:
    explicit VizRef(jlong handle) {
        auto* v = reinterpret_cast<geode_viz*>(handle);
        {
            std::lock_guard<std::mutex> guard(gVizMutex);
            auto it = gVizLive.find(v);
            if (it == gVizLive.end()) return;
            entry_ = it->second;
        }
        lock_ = std::shared_lock<std::shared_mutex>(entry_->lock);
        viz_ = v;
    }
    VizRef(const VizRef&) = delete;
    VizRef& operator=(const VizRef&) = delete;
    operator geode_viz*() const { return viz_; }

private:
    std::shared_ptr<VizEntry> entry_;  // declared before lock_: the lock is released first
    std::shared_lock<std::shared_mutex> lock_;
    geode_viz* viz_ = nullptr;
};

VizRef vizOf(jlong handle) { return VizRef(handle); }

void vizRegister(geode_viz* v) {
    if (!v) return;
    std::lock_guard<std::mutex> guard(gVizMutex);
    gVizLive[v] = std::make_shared<VizEntry>();
}

void vizDestroyLive(jlong handle) {
    auto* v = reinterpret_cast<geode_viz*>(handle);
    std::shared_ptr<VizEntry> entry;
    {
        std::lock_guard<std::mutex> guard(gVizMutex);
        auto it = gVizLive.find(v);
        if (it == gVizLive.end()) return;  // stale or already destroyed
        entry = std::move(it->second);
        gVizLive.erase(it);
    }
    std::unique_lock<std::shared_mutex> exclusive(entry->lock);  // waits for calls in flight
    geode_viz_destroy(v);
}
geode_export_pass* passOf(jlong handle) { return reinterpret_cast<geode_export_pass*>(handle); }
AAssetManager* assetsOf(JNIEnv* env, jobject assetManager) { return assetManager ? AAssetManager_fromJava(env, assetManager) : nullptr; }

// A direct ByteBuffer's bytes when it holds at least `needed` of them, else null (a heap buffer has no address).
uint8_t* directBytes(JNIEnv* env, jobject buffer, int64_t needed) {
    if (!buffer || needed <= 0) return nullptr;
    auto* bytes = static_cast<uint8_t*>(env->GetDirectBufferAddress(buffer));
    return bytes && env->GetDirectBufferCapacity(buffer) >= needed ? bytes : nullptr;
}

int64_t rgbaBytes(jint width, jint height) { return static_cast<int64_t>(width) * static_cast<int64_t>(height) * 4; }

}  // namespace

extern "C" {

JNIEXPORT jlong JNICALL
Java_dev_geode_engine_bridge_GeodeNative_vizCreate(JNIEnv* env, jobject, jobject assetManager, jstring cacheDir) {
    Utf8Chars dir(env, cacheDir);
    AAssetManager* assets = assetManager ? AAssetManager_fromJava(env, assetManager) : nullptr;
    geode_viz* created = geode_viz_create(assets, dir.get());
    vizRegister(created);
    return reinterpret_cast<jlong>(created);
}

JNIEXPORT void JNICALL
Java_dev_geode_engine_bridge_GeodeNative_vizDestroy(JNIEnv*, jobject, jlong handle) {
    vizDestroyLive(handle);
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
Java_dev_geode_engine_bridge_GeodeNative_vizLoadMilkPreset(JNIEnv* env, jobject, jlong handle, jstring path) {
    Utf8Chars file(env, path);
    geode_viz_load_milk_preset(vizOf(handle), file.get());
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
    return geode::jni::utf8String(env, buffer);
}

JNIEXPORT jint JNICALL
Java_dev_geode_engine_bridge_GeodeNative_vizTakeDirectorStyleMoments(JNIEnv*, jobject, jlong handle) {
    return geode_viz_take_director_style_moments(vizOf(handle));
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
Java_dev_geode_engine_bridge_GeodeNative_vizSetSoundTuning(JNIEnv* env, jobject, jlong handle, jfloatArray saved) {
    std::vector<float> in(saved ? static_cast<size_t>(env->GetArrayLength(saved)) : 0);
    if (!in.empty()) env->GetFloatArrayRegion(saved, 0, static_cast<jsize>(in.size()), in.data());
    geode_viz_set_sound_tuning(vizOf(handle), saved ? in.data() : nullptr, static_cast<int>(in.size()));
}

JNIEXPORT void JNICALL
Java_dev_geode_engine_bridge_GeodeNative_macroBankSet(JNIEnv* env, jobject, jstring bank) {
    Utf8Chars text(env, bank);
    geode_macro_bank_set(text.get());
}

JNIEXPORT jstring JNICALL
Java_dev_geode_engine_bridge_GeodeNative_macroBankDefault(JNIEnv* env, jobject) {
    return env->NewStringUTF(geode_macro_bank_default());
}

JNIEXPORT jstring JNICALL
Java_dev_geode_engine_bridge_GeodeNative_macroBankNormalize(JNIEnv* env, jobject, jstring bank) {
    Utf8Chars text(env, bank);
    return env->NewStringUTF(geode_macro_bank_normalize(text.get()));
}

JNIEXPORT jstring JNICALL
Java_dev_geode_engine_bridge_GeodeNative_macroBankSetValue(JNIEnv* env, jobject, jstring bank, jint knobId, jfloat value) {
    Utf8Chars text(env, bank);
    return env->NewStringUTF(geode_macro_bank_set_value(text.get(), knobId, value));
}

JNIEXPORT jstring JNICALL
Java_dev_geode_engine_bridge_GeodeNative_macroBankRename(JNIEnv* env, jobject, jstring bank, jint knobId, jstring name) {
    Utf8Chars text(env, bank);
    Utf8Chars label(env, name);
    return env->NewStringUTF(geode_macro_bank_rename(text.get(), knobId, label.get()));
}

JNIEXPORT jstring JNICALL
Java_dev_geode_engine_bridge_GeodeNative_macroBankAddKnob(JNIEnv* env, jobject, jstring bank, jstring name) {
    Utf8Chars text(env, bank);
    Utf8Chars label(env, name);
    return env->NewStringUTF(geode_macro_bank_add_knob(text.get(), label.get()));
}

JNIEXPORT jstring JNICALL
Java_dev_geode_engine_bridge_GeodeNative_macroBankRemoveKnob(JNIEnv* env, jobject, jstring bank, jint knobId) {
    Utf8Chars text(env, bank);
    return env->NewStringUTF(geode_macro_bank_remove_knob(text.get(), knobId));
}

JNIEXPORT jstring JNICALL
Java_dev_geode_engine_bridge_GeodeNative_macroBankDuplicateKnob(JNIEnv* env, jobject, jstring bank, jint knobId) {
    Utf8Chars text(env, bank);
    return env->NewStringUTF(geode_macro_bank_duplicate_knob(text.get(), knobId));
}

JNIEXPORT jstring JNICALL
Java_dev_geode_engine_bridge_GeodeNative_macroBankResetKnob(JNIEnv* env, jobject, jstring bank, jint knobId) {
    Utf8Chars text(env, bank);
    return env->NewStringUTF(geode_macro_bank_reset_knob(text.get(), knobId));
}

JNIEXPORT jstring JNICALL
Java_dev_geode_engine_bridge_GeodeNative_macroBankMoveKnob(JNIEnv* env, jobject, jstring bank, jint knobId, jint delta) {
    Utf8Chars text(env, bank);
    return env->NewStringUTF(geode_macro_bank_move_knob(text.get(), knobId, delta));
}

JNIEXPORT jstring JNICALL
Java_dev_geode_engine_bridge_GeodeNative_macroBankSetLink(JNIEnv* env, jobject, jstring bank, jint knobId, jstring target, jfloat up,
                                                          jfloat down) {
    Utf8Chars text(env, bank);
    Utf8Chars name(env, target);
    return env->NewStringUTF(geode_macro_bank_set_link(text.get(), knobId, name.get(), up, down));
}

JNIEXPORT jstring JNICALL
Java_dev_geode_engine_bridge_GeodeNative_macroBankAddLink(JNIEnv* env, jobject, jstring bank, jint knobId, jstring target) {
    Utf8Chars text(env, bank);
    Utf8Chars name(env, target);
    return env->NewStringUTF(geode_macro_bank_add_link(text.get(), knobId, name.get()));
}

JNIEXPORT jstring JNICALL
Java_dev_geode_engine_bridge_GeodeNative_macroBankRemoveLink(JNIEnv* env, jobject, jstring bank, jint knobId, jstring target) {
    Utf8Chars text(env, bank);
    Utf8Chars name(env, target);
    return env->NewStringUTF(geode_macro_bank_remove_link(text.get(), knobId, name.get()));
}

JNIEXPORT jstring JNICALL
Java_dev_geode_engine_bridge_GeodeNative_macroBankEditor(JNIEnv* env, jobject, jstring bank, jstring styleId) {
    Utf8Chars text(env, bank);
    Utf8Chars style(env, styleId);
    return env->NewStringUTF(geode_macro_bank_editor(text.get(), style.get()));
}

JNIEXPORT jstring JNICALL
Java_dev_geode_engine_bridge_GeodeNative_macroBankSetCurve(JNIEnv* env, jobject, jstring bank, jint knobId, jstring target, jstring curve) {
    Utf8Chars text(env, bank);
    Utf8Chars name(env, target);
    Utf8Chars shape(env, curve);
    return env->NewStringUTF(geode_macro_bank_set_curve(text.get(), knobId, name.get(), shape.get()));
}

JNIEXPORT jstring JNICALL
Java_dev_geode_engine_bridge_GeodeNative_macroBankAddKnobFor(JNIEnv* env, jobject, jstring bank, jstring control) {
    Utf8Chars text(env, bank);
    Utf8Chars key(env, control);
    return env->NewStringUTF(geode_macro_bank_add_knob_for(text.get(), key.get()));
}

JNIEXPORT jstring JNICALL
Java_dev_geode_engine_bridge_GeodeNative_macroBankControls(JNIEnv* env, jobject, jstring bank) {
    Utf8Chars text(env, bank);
    return env->NewStringUTF(geode_macro_bank_controls(text.get()));
}

JNIEXPORT jstring JNICALL
Java_dev_geode_engine_bridge_GeodeNative_macroBankLiveMods(JNIEnv* env, jobject) {
    return env->NewStringUTF(geode_macro_bank_live_mods());
}

JNIEXPORT jstring JNICALL
Java_dev_geode_engine_bridge_GeodeNative_macroBankRollRanges(JNIEnv* env, jobject, jstring bank, jint knobId, jlong seed) {
    Utf8Chars text(env, bank);
    return env->NewStringUTF(geode_macro_bank_roll_ranges(text.get(), knobId, static_cast<long long>(seed)));
}

JNIEXPORT jstring JNICALL
Java_dev_geode_engine_bridge_GeodeNative_macroBankAddRandom(JNIEnv* env, jobject, jstring bank, jint knobId, jstring styleId, jint count,
                                                           jlong seed) {
    Utf8Chars text(env, bank);
    Utf8Chars style(env, styleId);
    return env->NewStringUTF(geode_macro_bank_add_random(text.get(), knobId, style.get(), count, static_cast<long long>(seed)));
}

JNIEXPORT jstring JNICALL
Java_dev_geode_engine_bridge_GeodeNative_macroBankRollValues(JNIEnv* env, jobject, jstring bank, jstring styleId, jlong seed) {
    Utf8Chars text(env, bank);
    Utf8Chars style(env, styleId);
    return env->NewStringUTF(geode_macro_bank_roll_values(text.get(), style.get(), static_cast<long long>(seed)));
}

JNIEXPORT jstring JNICALL
Java_dev_geode_engine_bridge_GeodeNative_macroBankHideKnob(JNIEnv* env, jobject, jstring bank, jint knobId) {
    Utf8Chars text(env, bank);
    return env->NewStringUTF(geode_macro_bank_hide_knob(text.get(), knobId));
}

JNIEXPORT jstring JNICALL
Java_dev_geode_engine_bridge_GeodeNative_macroBankShowKnob(JNIEnv* env, jobject, jstring bank, jint knobId) {
    Utf8Chars text(env, bank);
    return env->NewStringUTF(geode_macro_bank_show_knob(text.get(), knobId));
}

JNIEXPORT jstring JNICALL
Java_dev_geode_engine_bridge_GeodeNative_macroBankAutoName(JNIEnv* env, jobject, jstring bank, jint knobId) {
    Utf8Chars text(env, bank);
    return env->NewStringUTF(geode_macro_bank_auto_name(text.get(), knobId));
}

JNIEXPORT void JNICALL
Java_dev_geode_engine_bridge_GeodeNative_directorSet(JNIEnv*, jobject, jboolean on, jfloat amount, jlong seed) {
    geode_director_set(on == JNI_TRUE ? 1 : 0, amount, static_cast<long long>(seed));
}

JNIEXPORT void JNICALL
Java_dev_geode_engine_bridge_GeodeNative_directorReachSet(JNIEnv*, jobject, jint level) {
    geode_director_reach_set(level);
}

JNIEXPORT void JNICALL
Java_dev_geode_engine_bridge_GeodeNative_directorKeepSet(JNIEnv* env, jobject, jstring keys) {
    Utf8Chars text(env, keys);
    geode_director_keep_set(text.get());
}

JNIEXPORT void JNICALL
Java_dev_geode_engine_bridge_GeodeNative_noteResponseSet(JNIEnv*, jobject, jboolean enabled) {
    geode_note_response_set(enabled ? 1 : 0);
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
    return geode::jni::utf8String(env, buffer.data());
}

JNIEXPORT void JNICALL
Java_dev_geode_engine_bridge_GeodeNative_vizSetLfos(JNIEnv* env, jobject, jlong handle, jstring lfos) {
    Utf8Chars text(env, lfos);
    if (text.get()) geode_viz_set_lfos(vizOf(handle), text.get());
}

JNIEXPORT void JNICALL
Java_dev_geode_engine_bridge_GeodeNative_vizSetAdsrs(JNIEnv* env, jobject, jlong handle, jstring adsrs) {
    Utf8Chars text(env, adsrs);
    if (text.get()) geode_viz_set_adsrs(vizOf(handle), text.get());
}

// A null text is Auto: the style's own routes play.
JNIEXPORT void JNICALL
Java_dev_geode_engine_bridge_GeodeNative_vizSetReactions(JNIEnv* env, jobject, jlong handle, jstring reactions) {
    Utf8Chars text(env, reactions);
    if (reactions && !text.get()) return;  // could not read the text: keep the last one
    geode_viz_set_reactions(vizOf(handle), text.get());
}

JNIEXPORT void JNICALL
Java_dev_geode_engine_bridge_GeodeNative_handSet(JNIEnv*, jobject, jint mask, jboolean hasUser) {
    geode_hand_set(static_cast<unsigned>(mask), hasUser ? 1 : 0);
}

JNIEXPORT jint JNICALL
Java_dev_geode_engine_bridge_GeodeNative_musicActionsFor(JNIEnv* env, jobject, jstring styleId) {
    Utf8Chars id(env, styleId);
    return static_cast<jint>(geode_music_actions_for(id.get()));
}

JNIEXPORT void JNICALL
Java_dev_geode_engine_bridge_GeodeNative_vizRequestScene(JNIEnv* env, jobject, jlong handle, jstring sceneId) {
    Utf8Chars id(env, sceneId);
    geode_viz_request_scene(vizOf(handle), id.get());
}

JNIEXPORT void JNICALL
Java_dev_geode_engine_bridge_GeodeNative_vizSetQuality(JNIEnv*, jobject, jlong handle, jfloat renderScale, jfloat marchScale,
                                                      jboolean postFx, jboolean adaptive, jint liquidHotPolicy) {
    geode_viz_set_quality(vizOf(handle), renderScale, marchScale, postFx == JNI_TRUE ? 1 : 0, adaptive == JNI_TRUE ? 1 : 0,
                          liquidHotPolicy);
}

// The live frame: the renderer takes from the ring itself, so no sample crosses JNI. A ring handle is valid
// while its Kotlin holder is reachable, and the caller holds it for this call.
JNIEXPORT jint JNICALL
Java_dev_geode_engine_bridge_GeodeNative_vizFrame(JNIEnv*, jobject, jlong handle, jlong ring, jfloat pacedFps, jfloat capFps) {
    return geode_viz_frame(vizOf(handle), reinterpret_cast<const geode_pcm_ring*>(ring), pacedFps, capFps);
}

JNIEXPORT void JNICALL
Java_dev_geode_engine_bridge_GeodeNative_vizSetOffscreen(JNIEnv*, jobject, jlong handle, jboolean on) {
    geode_viz_set_offscreen(vizOf(handle), on == JNI_TRUE);
}

JNIEXPORT void JNICALL
Java_dev_geode_engine_bridge_GeodeNative_wallpaperWindow(JNIEnv* env, jobject, jfloat surfaceW, jfloat surfaceH,
                                                         jfloat displayW, jfloat displayH, jfloatArray offsets,
                                                         jfloatArray out) {
    if (!out || env->GetArrayLength(out) < 4) return;
    float raw[GEODE_WALLPAPER_OFFSETS] = {};
    jsize count = 0;
    if (offsets) {
        count = std::min<jsize>(env->GetArrayLength(offsets), GEODE_WALLPAPER_OFFSETS);
        env->GetFloatArrayRegion(offsets, 0, count, reinterpret_cast<jfloat*>(raw));
    }
    float window[4] = {};
    geode_wallpaper_window(surfaceW, surfaceH, displayW, displayH, raw, static_cast<int>(count), window);
    env->SetFloatArrayRegion(out, 0, 4, reinterpret_cast<const jfloat*>(window));
}

JNIEXPORT jstring JNICALL
Java_dev_geode_engine_bridge_GeodeNative_vizLastError(JNIEnv* env, jobject, jlong handle) {
    return geode::jni::utf8String(env, geode_viz_last_error(vizOf(handle)));
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
Java_dev_geode_engine_bridge_GeodeNative_vizRender(JNIEnv*, jobject, jlong handle, jdouble timeSeconds, jint targetFbo) {
    geode_viz_render(vizOf(handle), timeSeconds, static_cast<uint32_t>(targetFbo));
}

// pixels is a Java IntArray? (ARGB per Android's Bitmap.getPixels), so a null array is a valid "clear"
// call rather than a JNI error - handled before touching the (possibly null) array. width*height is
// checked against the array's actual length and treated as a
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
Java_dev_geode_engine_bridge_GeodeNative_catalogueStyleIds(JNIEnv* env, jobject, jint trait) {
    return env->NewStringUTF(geode_catalogue_style_ids(trait));
}

JNIEXPORT jstring JNICALL
Java_dev_geode_engine_bridge_GeodeNative_cataloguePickerNames(JNIEnv* env, jobject) {
    return env->NewStringUTF(geode_catalogue_picker_names());
}

JNIEXPORT jstring JNICALL
Java_dev_geode_engine_bridge_GeodeNative_cataloguePickerIds(JNIEnv* env, jobject, jstring family) {
    Utf8Chars name(env, family);
    return env->NewStringUTF(geode_catalogue_picker_ids(name.get()));
}

JNIEXPORT jstring JNICALL
Java_dev_geode_engine_bridge_GeodeNative_catalogueResolve(JNIEnv* env, jobject, jstring styleId) {
    Utf8Chars style(env, styleId);
    return env->NewStringUTF(geode_catalogue_resolve(style.get()));
}

JNIEXPORT jstring JNICALL
Java_dev_geode_engine_bridge_GeodeNative_cataloguePresetStyle(JNIEnv* env, jobject, jstring styleId) {
    Utf8Chars style(env, styleId);
    return env->NewStringUTF(geode_catalogue_preset_style(style.get()));
}

JNIEXPORT jstring JNICALL
Java_dev_geode_engine_bridge_GeodeNative_catalogueScopeOf(JNIEnv* env, jobject, jstring paramKey) {
    Utf8Chars key(env, paramKey);
    return env->NewStringUTF(geode_catalogue_scope_of(key.get()));
}

JNIEXPORT jboolean JNICALL
Java_dev_geode_engine_bridge_GeodeNative_catalogueReaches(JNIEnv* env, jobject, jstring scope, jstring styleId) {
    Utf8Chars name(env, scope);
    Utf8Chars style(env, styleId);
    return geode_catalogue_reaches(name.get(), style.get()) != 0 ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jfloat JNICALL
Java_dev_geode_engine_bridge_GeodeNative_catalogueOverride(JNIEnv* env, jobject, jstring styleId, jstring paramKey) {
    Utf8Chars style(env, styleId);
    Utf8Chars key(env, paramKey);
    return geode_catalogue_override(style.get(), key.get());
}

JNIEXPORT jfloat JNICALL
Java_dev_geode_engine_bridge_GeodeNative_catalogueValue(JNIEnv* env, jobject, jstring name) {
    Utf8Chars chars(env, name);
    return geode_catalogue_value(chars.get());
}

JNIEXPORT jstring JNICALL
Java_dev_geode_engine_bridge_GeodeNative_cataloguePresetsText(JNIEnv* env, jobject) {
    return env->NewStringUTF(geode_catalogue_presets_text());
}

JNIEXPORT jstring JNICALL
Java_dev_geode_engine_bridge_GeodeNative_paramsSchemaText(JNIEnv* env, jobject) {
    return env->NewStringUTF(geode_params_schema_text());
}

JNIEXPORT jstring JNICALL
Java_dev_geode_engine_bridge_GeodeNative_paramText(JNIEnv* env, jobject, jstring key, jint unit, jfloat value, jfloat lo,
                                                   jfloat hi, jfloat def) {
    Utf8Chars name(env, key);
    char text[96];
    geode_param_text(name.get(), unit, value, lo, hi, def, text, sizeof text);
    return env->NewStringUTF(text);
}

JNIEXPORT jboolean JNICALL
Java_dev_geode_engine_bridge_GeodeNative_sliderOffDefault(JNIEnv*, jobject, jfloat value, jfloat def) {
    return geode_slider_off_default(value, def) ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jint JNICALL Java_dev_geode_engine_bridge_GeodeNative_sliderIntSteps(JNIEnv*, jobject, jint first, jint last) {
    return geode_slider_int_steps(first, last);
}

JNIEXPORT jint JNICALL
Java_dev_geode_engine_bridge_GeodeNative_sliderIntValue(JNIEnv*, jobject, jfloat position, jint first, jint last) {
    return geode_slider_int_value(position, first, last);
}

JNIEXPORT jfloat JNICALL
Java_dev_geode_engine_bridge_GeodeNative_sliderLogPosition(JNIEnv*, jobject, jfloat value, jfloat lo, jfloat hi) {
    return geode_slider_log_position(value, lo, hi);
}

JNIEXPORT jfloat JNICALL
Java_dev_geode_engine_bridge_GeodeNative_sliderLogValue(JNIEnv*, jobject, jfloat position, jfloat lo, jfloat hi) {
    return geode_slider_log_value(position, lo, hi);
}

JNIEXPORT jfloat JNICALL
Java_dev_geode_engine_bridge_GeodeNative_paramsPaletteBase(JNIEnv*, jobject, jint palette, jfloat override) {
    return geode_params_palette_base(palette, override);
}

JNIEXPORT jfloat JNICALL
Java_dev_geode_engine_bridge_GeodeNative_paramsPaletteRange(JNIEnv*, jobject, jint palette, jfloat override) {
    return geode_params_palette_range(palette, override);
}

JNIEXPORT jint JNICALL
Java_dev_geode_engine_bridge_GeodeNative_colourFromHsv(JNIEnv*, jobject, jfloat h, jfloat s, jfloat v) {
    return geode_colour_from_hsv(h, s, v);
}

JNIEXPORT void JNICALL
Java_dev_geode_engine_bridge_GeodeNative_colourToHsv(JNIEnv* env, jobject, jint rgb, jfloatArray out) {
    if (!out || env->GetArrayLength(out) < 3) return;
    float hsv[3];
    geode_colour_to_hsv(rgb, hsv);
    env->SetFloatArrayRegion(out, 0, 3, hsv);
}

JNIEXPORT jint JNICALL
Java_dev_geode_engine_bridge_GeodeNative_colourParseHex(JNIEnv* env, jobject, jstring text) {
    Utf8Chars chars(env, text);
    return geode_colour_parse_hex(chars.get());
}

JNIEXPORT jstring JNICALL
Java_dev_geode_engine_bridge_GeodeNative_colourHex(JNIEnv* env, jobject, jint rgb) {
    return env->NewStringUTF(geode_colour_hex(rgb));
}

JNIEXPORT jint JNICALL
Java_dev_geode_engine_bridge_GeodeNative_colourAverageArgb(JNIEnv* env, jobject, jintArray argb) {
    if (!argb) return -1;
    const jsize n = env->GetArrayLength(argb);
    std::vector<int32_t> px(static_cast<size_t>(n));
    env->GetIntArrayRegion(argb, 0, n, reinterpret_cast<jint*>(px.data()));
    return geode_colour_average_argb(px.data(), n);
}

JNIEXPORT jint JNICALL
Java_dev_geode_engine_bridge_GeodeNative_colourPaletteAccent(JNIEnv*, jobject, jfloat base, jfloat range) {
    return geode_colour_palette_accent(base, range);
}

JNIEXPORT jfloatArray JNICALL
Java_dev_geode_engine_bridge_GeodeNative_artPaletteExtract(JNIEnv* env, jobject, jintArray argb) {
    if (!argb) return nullptr;
    const jsize n = env->GetArrayLength(argb);
    std::vector<int32_t> px(static_cast<size_t>(n));
    env->GetIntArrayRegion(argb, 0, n, reinterpret_cast<jint*>(px.data()));
    float got[3];
    if (!geode_art_palette_extract(px.data(), n, got)) return nullptr;
    jfloatArray out = env->NewFloatArray(3);
    if (out) env->SetFloatArrayRegion(out, 0, 3, got);
    return out;
}

JNIEXPORT jfloatArray JNICALL
Java_dev_geode_engine_bridge_GeodeNative_widgetGlow(JNIEnv* env, jobject, jfloat artHue, jfloat artSpan, jboolean hasStyle,
                                                    jint styleHash, jint tick) {
    float got[18];
    geode_widget_glow(artHue, artSpan, hasStyle ? 1 : 0, styleHash, tick, got);
    jfloatArray out = env->NewFloatArray(18);
    if (out) env->SetFloatArrayRegion(out, 0, 18, got);
    return out;
}

JNIEXPORT jstring JNICALL
Java_dev_geode_engine_bridge_GeodeNative_paletteColourMapName(JNIEnv* env, jobject, jint index) {
    return env->NewStringUTF(geode_palette_colour_map_name(index));
}

JNIEXPORT jintArray JNICALL
Java_dev_geode_engine_bridge_GeodeNative_paletteRamp(JNIEnv* env, jobject, jintArray stops, jint samples) {
    const jsize n = stops ? std::min<jsize>(env->GetArrayLength(stops), 5) : 0;
    int in[5] = {-1, -1, -1, -1, -1};
    if (n > 0) env->GetIntArrayRegion(stops, 0, n, reinterpret_cast<jint*>(in));
    std::vector<int> out(static_cast<size_t>(std::clamp<jint>(samples, 0, kMaxSwatch)));
    geode_palette_ramp(in, n, static_cast<int>(out.size()), out.data());
    return toJava(env, out);
}

JNIEXPORT jintArray JNICALL
Java_dev_geode_engine_bridge_GeodeNative_paletteStopsFromHues(JNIEnv* env, jobject, jfloat base, jfloat range, jint count) {
    std::vector<int> out(static_cast<size_t>(std::clamp<jint>(count, 0, 5)));
    geode_palette_stops_from_hues(base, range, static_cast<int>(out.size()), out.data());
    return toJava(env, out);
}

JNIEXPORT jintArray JNICALL
Java_dev_geode_engine_bridge_GeodeNative_paramsPalettePreview(JNIEnv* env, jobject, jfloatArray params, jboolean second, jint samples) {
    std::vector<int> out(static_cast<size_t>(std::clamp<jint>(samples, 0, kMaxSwatch)));
    if (!params || env->GetArrayLength(params) < geode_viz_param_count()) return toJava(env, out);
    std::vector<float> wire(static_cast<size_t>(env->GetArrayLength(params)));
    env->GetFloatArrayRegion(params, 0, static_cast<jsize>(wire.size()), wire.data());
    geode_params_palette_preview(wire.data(), second ? 1 : 0, static_cast<int>(out.size()), out.data());
    return toJava(env, out);
}

JNIEXPORT jfloat JNICALL
Java_dev_geode_engine_bridge_GeodeNative_touchZoom(JNIEnv*, jobject, jfloat current, jfloat scaleFactor) {
    return geode_touch_zoom(current, scaleFactor);
}

JNIEXPORT jfloat JNICALL
Java_dev_geode_engine_bridge_GeodeNative_touchRotation(JNIEnv*, jobject, jfloat current, jfloat degrees) {
    return geode_touch_rotation(current, degrees);
}

JNIEXPORT void JNICALL
Java_dev_geode_engine_bridge_GeodeNative_touchToNdc(JNIEnv* env, jobject, jfloatArray xy, jint points, jfloat width, jfloat height) {
    if (!xy) return;
    const jsize n = std::clamp<jsize>(points, 0, env->GetArrayLength(xy) / 2);
    if (n == 0) return;
    std::vector<float> buf(static_cast<size_t>(n) * 2);
    env->GetFloatArrayRegion(xy, 0, n * 2, buf.data());
    geode_touch_to_ndc(buf.data(), static_cast<int>(n), width, height);
    env->SetFloatArrayRegion(xy, 0, n * 2, buf.data());
}

JNIEXPORT void JNICALL
Java_dev_geode_engine_bridge_GeodeNative_touchPair(JNIEnv* env, jobject, jfloatArray start, jfloatArray last, jfloatArray now, jfloatArray out) {
    if (!start || !last || !now || !out) return;
    if (env->GetArrayLength(start) < 4 || env->GetArrayLength(last) < 4 || env->GetArrayLength(now) < 4 || env->GetArrayLength(out) < 6) return;
    float s[4], l[4], n[4], r[6];
    env->GetFloatArrayRegion(start, 0, 4, s);
    env->GetFloatArrayRegion(last, 0, 4, l);
    env->GetFloatArrayRegion(now, 0, 4, n);
    geode_touch_pair(s, l, n, r);
    env->SetFloatArrayRegion(out, 0, 6, r);
}

JNIEXPORT jint JNICALL
Java_dev_geode_engine_bridge_GeodeNative_touchPulse(JNIEnv* env, jobject, jint step, jfloatArray xy) {
    if (!xy || env->GetArrayLength(xy) < 4) return 0;
    float p[4];
    const int holdMs = geode_touch_pulse(step, p);
    if (holdMs > 0) env->SetFloatArrayRegion(xy, 0, 4, p);
    return holdMs;
}

JNIEXPORT jstring JNICALL
Java_dev_geode_engine_bridge_GeodeNative_customizeText(JNIEnv* env, jobject) {
    return env->NewStringUTF(geode_customize_text());
}

JNIEXPORT jstring JNICALL
Java_dev_geode_engine_bridge_GeodeNative_customizeLockableKeys(JNIEnv* env, jobject) {
    return env->NewStringUTF(geode_customize_lockable_keys());
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

JNIEXPORT jint JNICALL
Java_dev_geode_engine_bridge_GeodeNative_customizeChangedCount(JNIEnv* env, jobject, jfloatArray in) {
    if (!in || env->GetArrayLength(in) < geode_viz_param_count()) return 0;
    std::vector<float> input(static_cast<size_t>(geode_viz_param_count()));
    env->GetFloatArrayRegion(in, 0, static_cast<jsize>(input.size()), input.data());
    return geode_customize_changed_count(input.data());
}

JNIEXPORT void JNICALL
Java_dev_geode_engine_bridge_GeodeNative_customizeBorrow(JNIEnv* env, jobject, jstring category, jfloatArray in, jfloatArray from,
                                                         jfloatArray out) {
    const jsize count = static_cast<jsize>(geode_viz_param_count());
    if (!in || !from || !out || env->GetArrayLength(in) < count || env->GetArrayLength(from) < count || env->GetArrayLength(out) < count) {
        return;
    }
    Utf8Chars categoryName(env, category);
    std::vector<float> input(static_cast<size_t>(count));
    std::vector<float> source(input.size());
    std::vector<float> output(input.size());
    env->GetFloatArrayRegion(in, 0, count, input.data());
    env->GetFloatArrayRegion(from, 0, count, source.data());
    geode_customize_borrow(categoryName.get(), input.data(), source.data(), output.data());
    env->SetFloatArrayRegion(out, 0, count, output.data());
}

JNIEXPORT jstring JNICALL
Java_dev_geode_engine_bridge_GeodeNative_randomizeAllPick(JNIEnv* env, jobject, jlong seed, jstring current, jstring lockedStyles,
                                                          jstring lookStyles, jint milkCount, jintArray picks) {
    if (!picks || env->GetArrayLength(picks) < 2) return nullptr;
    Utf8Chars style(env, current);
    Utf8Chars locked(env, lockedStyles);
    Utf8Chars looks(env, lookStyles);
    char picked[128];
    int look = -1;
    int milk = -1;
    const int ok = geode_randomize_all_pick(static_cast<uint64_t>(seed), style.get(), locked.get(), looks.get(), milkCount, picked,
                                            static_cast<int>(sizeof picked), &look, &milk);
    const jint out[2] = {look, milk};
    env->SetIntArrayRegion(picks, 0, 2, out);
    return ok ? env->NewStringUTF(picked) : nullptr;
}

JNIEXPORT jboolean JNICALL
Java_dev_geode_engine_bridge_GeodeNative_randomizeAllRoll(JNIEnv* env, jobject, jstring styleId, jstring locked, jlong seed,
                                                          jboolean rollCategories, jfloatArray before, jfloatArray base,
                                                          jfloatArray out) {
    const jsize count = static_cast<jsize>(geode_viz_param_count());
    if (!before || !base || !out || env->GetArrayLength(before) < count || env->GetArrayLength(base) < count ||
        env->GetArrayLength(out) < count) {
        return JNI_FALSE;
    }
    Utf8Chars style(env, styleId);
    Utf8Chars lockedKeys(env, locked);
    std::vector<float> was(static_cast<size_t>(count));
    std::vector<float> start(was.size());
    std::vector<float> rolled(was.size());
    env->GetFloatArrayRegion(before, 0, count, was.data());
    env->GetFloatArrayRegion(base, 0, count, start.data());
    const int changed =
        geode_randomize_all_roll(style.get(), lockedKeys.get(), static_cast<uint64_t>(seed), rollCategories ? 1 : 0, was.data(),
                                 start.data(), rolled.data());
    env->SetFloatArrayRegion(out, 0, count, rolled.data());
    return changed ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jstring JNICALL
Java_dev_geode_engine_bridge_GeodeNative_configSchemaText(JNIEnv* env, jobject) {
    return env->NewStringUTF(geode_config_schema_text());
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

// Fixed-rate frame clock (Cast): the state is a caller-owned long[GEODE_FRAME_CLOCK_STATE], copied in and out.
JNIEXPORT void JNICALL
Java_dev_geode_engine_bridge_GeodeNative_frameClockStart(JNIEnv* env, jobject, jlongArray state, jint fps, jlong nowNs) {
    if (!state || env->GetArrayLength(state) < GEODE_FRAME_CLOCK_STATE) return;
    int64_t s[GEODE_FRAME_CLOCK_STATE] = {};
    geode_frame_clock_start(s, fps, nowNs);
    env->SetLongArrayRegion(state, 0, GEODE_FRAME_CLOCK_STATE, reinterpret_cast<const jlong*>(s));
}

JNIEXPORT jlong JNICALL
Java_dev_geode_engine_bridge_GeodeNative_frameClockWaitNs(JNIEnv* env, jobject, jlongArray state, jlong nowNs) {
    if (!state || env->GetArrayLength(state) < GEODE_FRAME_CLOCK_STATE) return 0;
    int64_t s[GEODE_FRAME_CLOCK_STATE];
    env->GetLongArrayRegion(state, 0, GEODE_FRAME_CLOCK_STATE, reinterpret_cast<jlong*>(s));
    return geode_frame_clock_wait_ns(s, nowNs);
}

JNIEXPORT jlong JNICALL
Java_dev_geode_engine_bridge_GeodeNative_frameClockTake(JNIEnv* env, jobject, jlongArray state, jlong nowNs) {
    if (!state || env->GetArrayLength(state) < GEODE_FRAME_CLOCK_STATE) return 0;
    int64_t s[GEODE_FRAME_CLOCK_STATE];
    env->GetLongArrayRegion(state, 0, GEODE_FRAME_CLOCK_STATE, reinterpret_cast<jlong*>(s));
    const int64_t pts = geode_frame_clock_take(s, nowNs);
    env->SetLongArrayRegion(state, 0, GEODE_FRAME_CLOCK_STATE, reinterpret_cast<const jlong*>(s));
    return pts;
}

JNIEXPORT jlong JNICALL
Java_dev_geode_engine_bridge_GeodeNative_frameClockAudioUntilUs(JNIEnv* env, jobject, jlongArray state) {
    if (!state || env->GetArrayLength(state) < GEODE_FRAME_CLOCK_STATE) return 0;
    int64_t s[GEODE_FRAME_CLOCK_STATE];
    env->GetLongArrayRegion(state, 0, GEODE_FRAME_CLOCK_STATE, reinterpret_cast<jlong*>(s));
    return geode_frame_clock_audio_until_us(s);
}

JNIEXPORT void JNICALL
Java_dev_geode_engine_bridge_GeodeNative_castFormat(JNIEnv* env, jobject, jintArray out) {
    if (!out || env->GetArrayLength(out) < GEODE_CAST_FORMAT_INTS) return;
    int f[GEODE_CAST_FORMAT_INTS];
    geode_cast_format(f);
    env->SetIntArrayRegion(out, 0, GEODE_CAST_FORMAT_INTS, reinterpret_cast<const jint*>(f));
}

// Export post-passes (core/viz/frame/ExportPasses): marshalling only.

JNIEXPORT jstring JNICALL
Java_dev_geode_engine_bridge_GeodeNative_exportPassError(JNIEnv* env, jobject, jlong handle) {
    return geode::jni::utf8String(env, geode_export_pass_error(passOf(handle)));
}

JNIEXPORT void JNICALL
Java_dev_geode_engine_bridge_GeodeNative_exportPassDestroy(JNIEnv*, jobject, jlong handle) {
    geode_export_pass_destroy(passOf(handle));
}

JNIEXPORT jint JNICALL
Java_dev_geode_engine_bridge_GeodeNative_exportWatermarkPlacement(JNIEnv*, jobject, jboolean required, jboolean proAdds,
                                                                 jint placement, jint use) {
    return geode_export_watermark_placement(required ? 1 : 0, proAdds ? 1 : 0, placement, use);
}

// The wordmark's paint as five floats: text px, alpha, letter spacing (em), shadow radius px, shadow alpha.
JNIEXPORT void JNICALL
Java_dev_geode_engine_bridge_GeodeNative_exportWatermarkPaint(JNIEnv* env, jobject, jint shortSide, jint placement,
                                                             jfloatArray out) {
    constexpr jsize kFloats = 5;
    geode_watermark_paint p{};
    geode_export_watermark_paint(shortSide, placement, &p);
    if (!out || env->GetArrayLength(out) < kFloats) return;
    const jfloat values[kFloats] = {p.text_px, static_cast<jfloat>(p.alpha), p.letter_spacing_em, p.shadow_radius_px,
                                    static_cast<jfloat>(p.shadow_alpha)};
    env->SetFloatArrayRegion(out, 0, kFloats, values);
}

// The tile and the text's place in it as seven floats: width, height, angle, pivot x, y, text x, y.
JNIEXPORT void JNICALL
Java_dev_geode_engine_bridge_GeodeNative_exportWatermarkRaster(JNIEnv* env, jobject, jfloat textWidth, jfloat ascent,
                                                              jfloat descent, jfloat textPx, jint placement,
                                                              jfloatArray out) {
    constexpr jsize kFloats = 7;
    geode_watermark_raster r{};
    geode_export_watermark_raster(textWidth, ascent, descent, textPx, placement, &r);
    if (!out || env->GetArrayLength(out) < kFloats) return;
    const jfloat values[kFloats] = {static_cast<jfloat>(r.width), static_cast<jfloat>(r.height), r.angle_degrees,
                                    r.pivot_x, r.pivot_y, r.text_x, r.text_y};
    env->SetFloatArrayRegion(out, 0, kFloats, values);
}

JNIEXPORT jlong JNICALL
Java_dev_geode_engine_bridge_GeodeNative_exportWatermarkCreate(JNIEnv* env, jobject, jobject assetManager, jobject rgba,
                                                              jint tileWidth, jint tileHeight, jint placement, jint corner,
                                                              jlong lengthMs) {
    const uint8_t* pixels = directBytes(env, rgba, rgbaBytes(tileWidth, tileHeight));
    return reinterpret_cast<jlong>(geode_export_watermark_create(assetsOf(env, assetManager), pixels, tileWidth, tileHeight,
                                                                 placement, corner, lengthMs));
}

JNIEXPORT void JNICALL
Java_dev_geode_engine_bridge_GeodeNative_exportWatermarkDraw(JNIEnv*, jobject, jlong handle, jint frameWidth, jint frameHeight,
                                                            jlong timeMs) {
    geode_export_watermark_draw(passOf(handle), frameWidth, frameHeight, timeMs);
}

JNIEXPORT jbyteArray JNICALL
Java_dev_geode_engine_bridge_GeodeNative_transitionPicker(JNIEnv* env, jobject, jobject assetManager) {
    // Bytes, not a jstring: NewStringUTF takes modified UTF-8, and the library's names are plain UTF-8.
    AAssetManager* assets = assetManager ? AAssetManager_fromJava(env, assetManager) : nullptr;
    const char* table = geode_transition_picker(assets);
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
Java_dev_geode_engine_bridge_GeodeNative_liveFpsChoices(JNIEnv* env, jobject, jfloatArray rates, jintArray out) {
    if (!out) return 0;
    FloatElements r(env, rates);
    int choices[GEODE_LIVE_FPS_MAX_CHOICES];
    const int n = geode_live_fps_choices(r.get(), r.length(), choices, GEODE_LIVE_FPS_MAX_CHOICES);
    const jsize written = std::min<jsize>({static_cast<jsize>(n), env->GetArrayLength(out), GEODE_LIVE_FPS_MAX_CHOICES});
    if (written > 0) env->SetIntArrayRegion(out, 0, written, reinterpret_cast<const jint*>(choices));
    return n;
}

JNIEXPORT jint JNICALL
Java_dev_geode_engine_bridge_GeodeNative_liveFpsCap(JNIEnv* env, jobject, jint storedFps, jfloatArray rates) {
    FloatElements r(env, rates);
    return geode_live_fps_cap(storedFps, r.get(), r.length());
}

JNIEXPORT jint JNICALL
Java_dev_geode_engine_bridge_GeodeNative_perfModeForDevice(JNIEnv*, jobject, jlong totalRamBytes, jboolean lowRam,
                                                          jint mediaPerformanceClass) {
    return geode_perf_mode_for_device(totalRamBytes, lowRam ? 1 : 0, mediaPerformanceClass);
}

// The background image bake (core/viz/Underlay). The rect comes back as a new int[4] (x, y, width, height).
JNIEXPORT jintArray JNICALL
Java_dev_geode_engine_bridge_GeodeNative_underlayCropRect(JNIEnv* env, jobject, jint srcW, jint srcH, jint targetW, jint targetH) {
    jint rect[4] = {0, 0, 0, 0};
    geode_underlay_crop_rect(srcW, srcH, targetW, targetH, rect);
    jintArray result = env->NewIntArray(4);
    if (result) env->SetIntArrayRegion(result, 0, 4, rect);
    return result;
}

// In place on width*height ARGB ints; a size the array cannot hold does nothing.
JNIEXPORT void JNICALL
Java_dev_geode_engine_bridge_GeodeNative_underlayFinish(JNIEnv* env, jobject, jintArray pixels, jint width, jint height,
                                                        jint blurRadius, jfloat dim) {
    if (!pixels || width <= 0 || height <= 0) return;
    const jlong count = static_cast<jlong>(width) * static_cast<jlong>(height);
    if (count > env->GetArrayLength(pixels)) return;
    std::vector<uint32_t> data(static_cast<size_t>(count));
    env->GetIntArrayRegion(pixels, 0, static_cast<jsize>(count), reinterpret_cast<jint*>(data.data()));
    geode_underlay_finish(data.data(), width, height, blurRadius, dim);
    env->SetIntArrayRegion(pixels, 0, static_cast<jsize>(count), reinterpret_cast<const jint*>(data.data()));
}

JNIEXPORT jfloat JNICALL
Java_dev_geode_engine_bridge_GeodeNative_underlayClampAmount(JNIEnv*, jobject, jfloat amount) {
    return geode_underlay_clamp_amount(amount);
}

JNIEXPORT jfloat JNICALL
Java_dev_geode_engine_bridge_GeodeNative_underlayClampDim(JNIEnv*, jobject, jfloat dim) {
    return geode_underlay_clamp_dim(dim);
}

JNIEXPORT jint JNICALL
Java_dev_geode_engine_bridge_GeodeNative_underlayClampBlur(JNIEnv*, jobject, jint radius) {
    return geode_underlay_clamp_blur(radius);
}

JNIEXPORT jint JNICALL
Java_dev_geode_engine_bridge_GeodeNative_underlayBlurMax(JNIEnv*, jobject) {
    return geode_underlay_blur_max();
}

JNIEXPORT jint JNICALL
Java_dev_geode_engine_bridge_GeodeNative_underlayBlendCount(JNIEnv*, jobject) {
    return geode_underlay_blend_count();
}

JNIEXPORT jstring JNICALL
Java_dev_geode_engine_bridge_GeodeNative_underlayBlendName(JNIEnv* env, jobject, jint blend) {
    return env->NewStringUTF(geode_underlay_blend_name(blend));
}

JNIEXPORT jint JNICALL
Java_dev_geode_engine_bridge_GeodeNative_underlayBlendOf(JNIEnv* env, jobject, jstring name) {
    if (!name) return geode_underlay_blend_of(nullptr);
    Utf8Chars text(env, name);
    return geode_underlay_blend_of(text.get());
}

JNIEXPORT jint JNICALL
Java_dev_geode_engine_bridge_GeodeNative_thermalTakeWarmNote(JNIEnv*, jobject, jfloat capFps, jfloat pacedFps) {
    return geode_thermal_take_warm_note(capFps, pacedFps);
}

JNIEXPORT void JNICALL
Java_dev_geode_engine_bridge_GeodeNative_memoryTrim(JNIEnv*, jobject, jint level) {
    geode_memory_trim(level);
}

// Cast's audio pump: the state is a caller-owned long[GEODE_CAST_AUDIO_STATE], a step's result a long[GEODE_CAST_AUDIO_STEP].
JNIEXPORT jint JNICALL
Java_dev_geode_engine_bridge_GeodeNative_castAudioStart(JNIEnv* env, jobject, jlongArray state, jint rateHz) {
    if (!state || env->GetArrayLength(state) < GEODE_CAST_AUDIO_STATE) return 0;
    int64_t s[GEODE_CAST_AUDIO_STATE] = {};
    const int capacity = geode_cast_audio_start(s, rateHz);
    env->SetLongArrayRegion(state, 0, GEODE_CAST_AUDIO_STATE, reinterpret_cast<const jlong*>(s));
    return capacity;
}

JNIEXPORT void JNICALL
Java_dev_geode_engine_bridge_GeodeNative_castAudioStep(JNIEnv* env, jobject, jlongArray state, jint gainedFrames,
                                                       jlong untilUs, jlongArray out) {
    if (!state || !out || env->GetArrayLength(state) < GEODE_CAST_AUDIO_STATE ||
        env->GetArrayLength(out) < GEODE_CAST_AUDIO_STEP) return;
    int64_t s[GEODE_CAST_AUDIO_STATE];
    int64_t o[GEODE_CAST_AUDIO_STEP] = {};
    env->GetLongArrayRegion(state, 0, GEODE_CAST_AUDIO_STATE, reinterpret_cast<jlong*>(s));
    geode_cast_audio_step(s, gainedFrames, untilUs, o);
    env->SetLongArrayRegion(state, 0, GEODE_CAST_AUDIO_STATE, reinterpret_cast<const jlong*>(s));
    env->SetLongArrayRegion(out, 0, GEODE_CAST_AUDIO_STEP, reinterpret_cast<const jlong*>(o));
}

// The AAC encoder's backlog (core/viz/frame/CastAudio, CastAudioFeed): a caller-owned long[GEODE_CAST_AUDIO_FEED_STATE].
JNIEXPORT jint JNICALL
Java_dev_geode_engine_bridge_GeodeNative_castAudioFeedStart(JNIEnv* env, jobject, jlongArray state, jint rateHz) {
    if (!state || env->GetArrayLength(state) < GEODE_CAST_AUDIO_FEED_STATE) return 0;
    int64_t s[GEODE_CAST_AUDIO_FEED_STATE] = {};
    const int capacity = geode_cast_audio_feed_start(s, rateHz);
    env->SetLongArrayRegion(state, 0, GEODE_CAST_AUDIO_FEED_STATE, reinterpret_cast<const jlong*>(s));
    return capacity;
}

JNIEXPORT jint JNICALL
Java_dev_geode_engine_bridge_GeodeNative_castAudioFeedAdd(JNIEnv* env, jobject, jlongArray state, jint frames) {
    if (!state || env->GetArrayLength(state) < GEODE_CAST_AUDIO_FEED_STATE) return 0;
    int64_t s[GEODE_CAST_AUDIO_FEED_STATE];
    env->GetLongArrayRegion(state, 0, GEODE_CAST_AUDIO_FEED_STATE, reinterpret_cast<jlong*>(s));
    const int64_t drop = geode_cast_audio_feed_add(s, frames);
    env->SetLongArrayRegion(state, 0, GEODE_CAST_AUDIO_FEED_STATE, reinterpret_cast<const jlong*>(s));
    return static_cast<jint>(drop);
}

JNIEXPORT jint JNICALL
Java_dev_geode_engine_bridge_GeodeNative_castAudioFeedTake(JNIEnv* env, jobject, jlongArray state, jint bufferFrames,
                                                           jlongArray ptsUs) {
    if (!state || !ptsUs || env->GetArrayLength(state) < GEODE_CAST_AUDIO_FEED_STATE || env->GetArrayLength(ptsUs) < 1) {
        return 0;
    }
    int64_t s[GEODE_CAST_AUDIO_FEED_STATE];
    int64_t pts = 0;
    env->GetLongArrayRegion(state, 0, GEODE_CAST_AUDIO_FEED_STATE, reinterpret_cast<jlong*>(s));
    const int64_t n = geode_cast_audio_feed_take(s, bufferFrames, &pts);
    env->SetLongArrayRegion(state, 0, GEODE_CAST_AUDIO_FEED_STATE, reinterpret_cast<const jlong*>(s));
    env->SetLongArrayRegion(ptsUs, 0, 1, reinterpret_cast<const jlong*>(&pts));
    return static_cast<jint>(n);
}

// Show on a computer's state and typed code (core/viz/frame/LinkShare, LinkPair) and its QR code
// (core/util/QrCode): marshalling only. The server and its routes are native (geode_cast_jni.cpp).
JNIEXPORT jint JNICALL
Java_dev_geode_engine_bridge_GeodeNative_linkStep(JNIEnv* env, jobject, jintArray state, jint event, jint arg) {
    if (!state || env->GetArrayLength(state) < GEODE_LINK_STATE) return 0;
    int s[GEODE_LINK_STATE];
    env->GetIntArrayRegion(state, 0, GEODE_LINK_STATE, reinterpret_cast<jint*>(s));
    const int actions = geode_link_step(s, event, arg);
    env->SetIntArrayRegion(state, 0, GEODE_LINK_STATE, reinterpret_cast<const jint*>(s));
    return actions;
}

JNIEXPORT jstring JNICALL
Java_dev_geode_engine_bridge_GeodeNative_linkPairCode(JNIEnv* env, jobject, jlong nowMs) {
    char text[32];
    geode_link_pair_code(nowMs, text, sizeof text);
    return geode::jni::utf8String(env, text);
}

JNIEXPORT jint JNICALL
Java_dev_geode_engine_bridge_GeodeNative_linkReceiverHealth(JNIEnv*, jobject, jlong nowMs, jlong loadMs, jlong lastFetchMs,
                                                            jint playerState, jint idleReason) {
    return geode_link_receiver_health(nowMs, loadMs, lastFetchMs, playerState, idleReason);
}

JNIEXPORT jint JNICALL
Java_dev_geode_engine_bridge_GeodeNative_qrEncode(JNIEnv* env, jobject, jstring text, jbyteArray out) {
    if (!out || env->GetArrayLength(out) < GEODE_QR_MAX_MODULES) return 0;
    geode::jni::Utf8Chars t(env, text);
    if (!t.get()) return 0;
    const size_t n = std::strlen(t.get());
    std::vector<uint8_t> modules(GEODE_QR_MAX_MODULES);
    const int side = geode_qr_encode(reinterpret_cast<const uint8_t*>(t.get()), static_cast<int>(n), modules.data(),
                                     static_cast<int>(modules.size()));
    if (side > 0) env->SetByteArrayRegion(out, 0, side * side, reinterpret_cast<const jbyte*>(modules.data()));
    return side;
}

// Record (output/Recorder, viz/frame/RecordTap): a live video of the visual and its sound.

JNIEXPORT jlong JNICALL Java_dev_geode_engine_bridge_GeodeNative_recordNowNs(JNIEnv*, jobject) {
    return geode_record_now_ns();
}

JNIEXPORT jlong JNICALL Java_dev_geode_engine_bridge_GeodeNative_recorderCreate(JNIEnv*, jobject, jint stageWidth,
                                                                                 jint stageHeight, jint fps, jint maxSec,
                                                                                 jlong capMs) {
    return reinterpret_cast<jlong>(geode_recorder_create(stageWidth, stageHeight, fps, maxSec, capMs));
}

JNIEXPORT void JNICALL Java_dev_geode_engine_bridge_GeodeNative_recorderDestroy(JNIEnv*, jobject, jlong handle) {
    geode_recorder_destroy(reinterpret_cast<geode_recorder*>(handle));
}

JNIEXPORT void JNICALL Java_dev_geode_engine_bridge_GeodeNative_recorderStop(JNIEnv*, jobject, jlong handle, jint why,
                                                                              jlong nowNs) {
    geode_recorder_stop(reinterpret_cast<geode_recorder*>(handle), why, nowNs);
}

JNIEXPORT jint JNICALL Java_dev_geode_engine_bridge_GeodeNative_recorderStopped(JNIEnv*, jobject, jlong handle) {
    return geode_recorder_stopped(reinterpret_cast<const geode_recorder*>(handle));
}

JNIEXPORT jlong JNICALL Java_dev_geode_engine_bridge_GeodeNative_recorderRecordedMs(JNIEnv*, jobject, jlong handle,
                                                                                     jlong nowNs) {
    return geode_recorder_recorded_ms(reinterpret_cast<const geode_recorder*>(handle), nowNs);
}

JNIEXPORT jlong JNICALL Java_dev_geode_engine_bridge_GeodeNative_recorderLimitMs(JNIEnv*, jobject, jlong handle) {
    return geode_recorder_limit_ms(reinterpret_cast<const geode_recorder*>(handle));
}

JNIEXPORT jboolean JNICALL Java_dev_geode_engine_bridge_GeodeNative_recordWorthKeeping(JNIEnv*, jobject, jlong recordedMs) {
    return geode_record_worth_keeping(recordedMs) ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jlong JNICALL Java_dev_geode_engine_bridge_GeodeNative_recordSessionOpen(JNIEnv*, jobject, jlong recorder,
                                                                                   jint fd, jint stageWidth,
                                                                                   jint stageHeight, jint shortSide,
                                                                                   jint fps) {
    return reinterpret_cast<jlong>(geode_record_session_open(reinterpret_cast<const geode_recorder*>(recorder), fd,
                                                             stageWidth, stageHeight, shortSide, fps));
}

JNIEXPORT void JNICALL Java_dev_geode_engine_bridge_GeodeNative_recordSessionDestroy(JNIEnv*, jobject, jlong session) {
    geode_record_session_destroy(reinterpret_cast<geode_record_session*>(session));
}

JNIEXPORT void JNICALL Java_dev_geode_engine_bridge_GeodeNative_recordSessionSize(JNIEnv* env, jobject, jlong session,
                                                                                  jintArray out) {
    if (!out || env->GetArrayLength(out) < 2) return;
    int width = 0;
    int height = 0;
    geode_record_session_size(reinterpret_cast<const geode_record_session*>(session), &width, &height);
    const jint size[2] = {width, height};
    env->SetIntArrayRegion(out, 0, 2, size);
}

JNIEXPORT jint JNICALL Java_dev_geode_engine_bridge_GeodeNative_recordSessionState(JNIEnv*, jobject, jlong session) {
    return geode_record_session_state(reinterpret_cast<const geode_record_session*>(session));
}

// Cast's tee (viz/frame/CastTee): the stage's picture to the cast stream.

JNIEXPORT jlong JNICALL Java_dev_geode_engine_bridge_GeodeNative_castTeeOpen(JNIEnv*, jobject, jint width, jint height,
                                                                              jint fps) {
    return reinterpret_cast<jlong>(geode_cast_tee_open(width, height, fps));
}

JNIEXPORT jboolean JNICALL Java_dev_geode_engine_bridge_GeodeNative_castTeeDraw(JNIEnv*, jobject, jlong tee, jlong nowNs,
                                                                                 jint width, jint height) {
    return geode_cast_tee_draw(reinterpret_cast<geode_cast_tee*>(tee), nowNs, width, height) ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT void JNICALL Java_dev_geode_engine_bridge_GeodeNative_castTeeClose(JNIEnv*, jobject, jlong tee) {
    geode_cast_tee_close(reinterpret_cast<geode_cast_tee*>(tee));
}

JNIEXPORT jboolean JNICALL Java_dev_geode_engine_bridge_GeodeNative_castTeeAwaitStage(JNIEnv*, jobject, jboolean stageDraws,
                                                                                       jboolean fallbackMade,
                                                                                       jlong sinceOpenNs) {
    return geode_cast_tee_await_stage(stageDraws ? 1 : 0, fallbackMade ? 1 : 0, sinceOpenNs) ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jlong JNICALL Java_dev_geode_engine_bridge_GeodeNative_shakeCreate(JNIEnv*, jobject) {
    return reinterpret_cast<jlong>(geode_shake_create());
}

JNIEXPORT void JNICALL Java_dev_geode_engine_bridge_GeodeNative_shakeDestroy(JNIEnv*, jobject, jlong handle) {
    geode_shake_destroy(reinterpret_cast<geode_shake*>(handle));
}

JNIEXPORT jboolean JNICALL Java_dev_geode_engine_bridge_GeodeNative_shakeSample(JNIEnv*, jobject, jlong handle, jlong timeNs,
                                                                              jfloat x, jfloat y, jfloat z) {
    return geode_shake_sample(reinterpret_cast<geode_shake*>(handle), timeNs, x, y, z) ? JNI_TRUE : JNI_FALSE;
}

}
